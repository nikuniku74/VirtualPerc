# Audit del core — seguire il tempo, agganciarsi in fretta, non saltare

Tre cose che l'app deve fare, e questo documento le prende una per una: **stare
sul tempo**, **allinearsi in fretta**, **non fare salti che non hanno senso**.

Ogni numero qui viene da una sonda che si ricostruisce:

```bash
cmake --build build --target VPAlign
./build/VPAlign_artefacts/Release/VPAlign
```

`VPAlign` (`scripts/probe_align.cpp`) non usa né la rete né l'audio: guida
`TempoFollower` esattamente come lo guida `BeatTracker`, e guida `BeatDecoder`
con attivazioni della forma che BeatNet produce davvero. È deterministica.

Data: 24 agosto 2026. Albero: `claude/core-timing-audit-78xiel`.

---

## In breve

| # | Cosa | Gravità | Stato |
|---|---|---|---|
| 1 | La fase esce da **un solo picco**, il tempo da un fit su ventiquattro battiti | alta | da fare |
| 2 | Un aggancio a mezzo battito è **stabile**: si autoalimenta e non rientra | alta | da fare |
| 3 | Il trim del tempo chiudeva **metà** dell'errore e si fermava lì | media | **corretto** |
| 4 | Il rilevatore di "l'analisi è andata altrove" **non può scattare** | media | da fare |
| 5 | Allineare uno scarto di fase costa 2–5 s (fino a 13 s in LOW) | media | da fare |
| 6 | Lo snap di fase all'ingresso in FOLLOWING non è filtrato per grandezza | bassa | da fare |

Il punto 3 è l'unico corretto in questo passaggio: è un bug isolato, con una
misura netta prima e dopo, e non cambia niente di quello che si sente a parte
togliere un errore di velocità. Gli altri cambiano il comportamento udibile —
qui c'è la misura e la proposta, la decisione è di chi ascolta.

---

## 1. La fase esce da un solo picco

`BeatDecoder::observe` chiude così:

```cpp
const float newPeriod = 60.0f / std::max (kMinBpm, bpm);
if (lastBeatSec >= 0.0)
    phase = wrap01 ((timeSec - lastBeatSec) / newPeriod);
```

Il **periodo** arriva da un fit ai minimi quadrati su ventiquattro battiti —
`fitPeriod` calcola pendenza *e* intercetta, e l'intercetta viene buttata. La
**fase** arriva da `lastBeatSec`, cioè dall'ultimo picco accettato: uno solo. Il
tempo è mediato su ventiquattro misure, la posizione del battito su una.

Il risultato è che il jitter di attacco del singolo battito passa dritto
all'orologio, non attenuato:

| tempo | jitter in ingresso | rms della fase | peggio | scatti > 0.05 batt. in 35 s |
|---|---|---|---|---|
| 76 BPM | 0 | 0.3 ms | 0.4 ms | 0 |
| 76 BPM | 22 ms | **22.2 ms** | 57.8 ms | 11 |
| 132 BPM | 22 ms | **22.5 ms** | 57.8 ms | 36 |
| 168 BPM | 22 ms | 0.060 batt. | 0.177 batt. | 49 |

Senza jitter la fase è esatta: non c'è nessun errore sistematico da correggere,
la geometria di `analysisSampleFor` regge frame per frame. Con jitter realistico
l'errore in uscita **è** il jitter in ingresso, uno a uno. E non è rumore
continuo: sono **scatti**, uno per ogni picco accettato, fino a 0.18 di battito
in un colpo. Il PLL li assorbe con 0.9 s di smoothing, e quello smoothing è
esattamente il motivo per cui l'anello non può essere più stretto di così.

**Proposta.** Prendere la fase dall'intercetta del fit invece che dall'ultimo
picco. `fitPeriod` ha già `meanT` e `meanIdx`: la griglia è
`meanIdx + (t - meanT) / slope`, e la fase è la sua parte frazionaria. Su otto
battiti l'errore atteso scende di √8 (≈ 8 ms), su ventiquattro di √24 (≈ 4.5 ms),
e gli scatti spariscono perché ogni nuovo picco sposta la retta di un
ventiquattresimo invece di ridefinire l'origine. Costo: nessuno, il fit gira già.

---

## 2. Mezzo battito fuori, e ci resta

Questo è il caso peggiore ed è **stabile**, non transitorio.

L'ancora della fase è l'ultimo picco accettato. Il cancello che decide cosa è un
picco (`kOnGridTolerance = 0.18`) misura rispetto a quell'ancora. Se l'ancora
finisce una volta su un ottavo, tutti i battiti veri cadono a 0.5 dalla griglia,
vengono scartati come controtempi — e gli ottavi, che cadono a 1.0, vengono
accettati. L'errore si dà ragione da solo. Il cancello che dovrebbe salvarci
(`kGridStaleBeats = 2.5`, "se non atterra niente sulla griglia allora la griglia
è sbagliata") non scatta mai, perché sulla griglia sbagliata atterra tutto.

Misurato, senza nessun jitter, con un ottavo fra ogni coppia di battiti:

| tempo | ottavo a | bpm trovato | errore di fase | in fase? |
|---|---|---|---|---|
| 100 | 0.45 | 100.00 | 0.000 | sì |
| 132 | 0.45 | 132.00 | 0.001 | sì |
| **168** | **0.45** | **168.00** | **0.499 battiti** | **NO** |
| 100 | 0.60 | 200.00 | — | livello sbagliato |
| 132 | 0.60 | 52.80 | — | livello sbagliato |
| 168 | 0.60 | 67.20 | — | livello sbagliato |

A 168 BPM con ottavi a 0.45 il decoder riporta **168.00 BPM, esatto**, con
confidenza piena, mezzo battito fuori, per novanta secondi e senza rientrare.
Non è materiale inventato: `docs/AI_BEAT_TRACKING.md` misura l'attivazione a
mezzo battito dal battito fra 0.73 e 0.77 su un mix a 76 BPM con ottavi pieni.
0.45 è dentro il normale.

E l'orologio non ha modo di accorgersene: la fase che gli arriva è *coerente*,
ferma, ad alta confidenza. Semplicemente è quella sbagliata.

**Proposta.** La stessa della #1, e per lo stesso prezzo. L'ampiezza
dell'attivazione ripiegata sul periodo distingue il battito dal controtempo — è
esattamente il numero che `TempoEstimator::halfPhaseRatio` già calcola (0.13
ripiegando sul battito vero, 0.80 sul doppio). Il ripiegamento sa dov'è l'uno; è
il bin più alto. Va restituito insieme al periodo invece di essere scartato, e
usato per ancorare la griglia quando la fase del fit e quella del ripiegamento
sono a mezzo battito di distanza.

---

## 3. Il trim del tempo chiudeva metà dell'errore — corretto

`observeOnsetPhase` misura di quanto l'orologio scivola rispetto alla canzone e
ne ricava un trim in BPM. La deriva misurata è però quella che resta **dopo** il
trim già applicato, perché in quell'intervallo l'orologio girava a
`target + tempoTrim`. Il codice la trattava come la risposta intera:

```cpp
const float wantedTrim = std::clamp (-measuredErrorBpm, -3.5f, 3.5f);
tempoTrim += (wantedTrim - tempoTrim) * trust;     // T ← T + (S - target - 2T)·k
```

Punto fisso: `T = (S - target) / 2`. Metà. Con la canzone a 81 e l'orologio
ancorato a 80:

| durata | prima | dopo |
|---|---|---|
| 10 s | trim 0.270 | trim 0.295 |
| 20 s | trim 0.498 | trim 0.928 |
| 40 s | trim **0.500** | trim 0.999 |
| 80 s | trim **0.500** | trim **1.000** |

Prima si fermava a 0.500 e ci restava per sempre, a qualunque tempo e a
qualunque durata. L'altra metà dell'errore di velocità rimaneva addosso
all'anello di fase come una pendenza permanente, che mangiava margine di
sterzata e teneva il BPM sullo schermo mezzo punto lontano dal vero.

Corretto in `Source/Tracking/TempoFollower.cpp`: il trim è un integratore, la
misura è un incremento.

```cpp
tempoTrim = std::clamp (tempoTrim - measuredErrorBpm * trust, -3.5f, 3.5f);
```

Il polo passa da `1 - 2·trust` a `1 - trust`, quindi converge un po' più piano ed
è più stabile, non meno. Il test in `Tests/TestMain.cpp` chiedeva `trim > 0.40`,
cioè passava con 0.500: ora chiede `> 0.85` e tempo entro 0.15 BPM.

Attivo sotto TAP e in regime `fixed`.

---

## 4. Il rilevatore di ricalibrazione non può scattare

`BeatTracker::process` ha una via per accorgersi che l'analisi è finita su
un'altra canzone: se il BPM della rete si scosta dell'8% da `heldBpm` per 1.15 s
netti, entra in `retuning` — e all'uscita **rimette la fase a posto** con uno
snap e azzera i voti della battuta. È l'unica via veloce che c'è per una canzone
nuova.

Ma `heldBpm` è, cinque righe più in là, il tempo del follower:

```cpp
if (lockedOnce && follower.currentTempo() > 50.0f)
    heldBpm = follower.currentTempo();
```

e il follower sta già inseguendo la rete, con tau 0.55 s da agganciato. Il
confronto è quindi fra la rete e qualcosa che converge alla rete in meno di un
secondo. Misurato, con un gradino sul BPM del decoder a partire da 100:

| nuovo BPM | salto | sopra l'8% per | ricalibra? |
|---|---|---|---|
| 115 | +15% | 0.31 s | no |
| 130 | +30% | 0.62 s | no |
| 150 | +50% | 0.83 s | no |
| 175 | +75% | 0.97 s | no |
| 60 | −40% | 1.12 s | no |
| 200 | +100% | 0.00 s | no (l'ottava viene presa subito) |
| 50 | −50% | 0.00 s | no (idem) |

Nessuno scatta. Servirebbe un salto istantaneo di circa il 65% che non sia
un'ottava — e le ottave sono prese in un blocco solo da `setTargetTempo`, quindi
sono proprio quelle che il rilevatore non vede mai. Resta in teoria il caso di un
decoder che oscilla fra due tempi lontani abbastanza a lungo da tenere il
follower sempre in mezzo; su un gradino, che è come si presenta una canzone
nuova, non succede. In pratica `retuning` non si accende, e con lui non si
accendono lo snap di fase per canzone nuova, lo stato `RICALIBRO` sul display e
il ramo `retuning` di `canPlay`.

Conseguenza pratica: quando la canzone cambia, la fase si rimette a posto solo
per la via lenta (punto 5), oppure per caso — passando da `lowConfidence`, che
però chiede prima 4 s di confidenza sotto 0.22.

**Proposta.** Non riparare il confronto sui BPM: è la grandezza sbagliata. Che la
canzone sia cambiata si vede meglio sulla **fase** — un errore fermo sopra ~0.2
di battito per più di un secondo, mentre la confidenza è alta, non è un
musicista che rallenta, è un'altra griglia. Quello è il segnale su cui
ri-ancorare.

---

## 5. Quanto ci mette a mettersi sulla canzone

La fase si corregge cambiando velocità, mai spostando la griglia — è la scelta
giusta e il commento in `advance()` spiega perché (una griglia spostata sotto una
parte che suona raddoppia o salta un colpo). Ma la sterzata è limitata al 3.5%
(MEDIUM) / 5% (HIGH) del tempo, quindi il tempo di allineamento è
`scarto / limite` battiti e basta:

| segui | scarto 0.10 | 0.25 | 0.40 | 0.48 |
|---|---|---|---|---|
| HIGH | 1.79 s | 2.82 s | 4.11 s | 4.85 s |
| MEDIUM | 2.30 s | 3.89 s | 5.89 s | 7.01 s |
| LOW | 3.35 s | 7.04 s | 11.14 s | 13.34 s |

(120 BPM, analisi pulita. HIGH è il default spedito.) Ai tempi lenti va peggio in
secondi: uno scarto di 0.40 costa 6.5 s a 70 BPM.

Il residuo si assesta a 0.012 di battito ovunque — è `kPhaseFloor`, voluto, e a
120 BPM sono 6 ms. Il tempo di aggancio **non** dipende dal buffer (2.85 s a 64,
2.86 s a 1024): la costante di tempo è in secondi in tutto l'anello, e la
verifica lo conferma.

**Proposta**, in due pezzi che non toccano l'invariante "la griglia non torna mai
indietro":

1. **Quando non suona niente, spostare la griglia.** A shaker fermo — non armato,
   o in attesa di entrare — non c'è nessuna parte da disturbare, e l'anello si
   comporta lo stesso come se ce ne fosse una. Uno snap lì è gratis, e fa sì che
   START trovi la griglia già a posto invece di entrare e allinearsi dopo.
2. **Quando suona, legare il limite alla grandezza dell'errore.** Un errore di un
   quarto di battito non è la stessa cosa di un centesimo, e oggi hanno lo stesso
   tetto. Un limite che cresce con l'errore — per dire, fino al 20–25% — chiude
   un quarto di battito in un battito invece che in 5.6, e resta monotono, quindi
   nessun colpo viene raddoppiato o perso. È quello che fa un percussionista:
   quando è mezzo battito fuori non aspetta undici battiti, si sposta.

---

## 6. Lo snap all'ingresso in FOLLOWING non guarda quanto è grande

```cpp
if (currentState == following && prevState != following)
{
    if (tapAligned) tapAligned = false;
    else if (! tapHold && nnBpm > 50.0f && gridMuteSamples <= 0 && haveHyp)
        follower.snapPhase (songPhase);
}
```

Nessun filtro sulla grandezza dell'errore, e nessuna domanda su se lo shaker
stia suonando. Il ramo gemello, dieci righe sopra, ce l'ha:

```cpp
if (haveHyp && gridMuteSamples <= 0
    && std::fabs (wrapCentered (follower.beatPhase() - songPhase)) > 0.15f)
```

`snapPhase` alza `reanchor`, che **emette un impulso** sulla nuova fase se sono
passati più di `max(20 ms, mezzo impulso)` dall'ultimo. Per un tap è
esattamente quello che si vuole. Per un rientro automatico in FOLLOWING dopo un
calo di confidenza — cioè in mezzo a un pezzo, con la parte che suona — un colpo
extra a 70 ms dal precedente si sente come un inciampo, e per una correzione che
poteva valere un centesimo di battito.

**Proposta.** Non allineare la soglia a quella del ramo gemello: renderebbe
l'aggancio più lento, cioè il contrario di quello che serve. Separare invece le
due cose — spostare la griglia è una, forzare il colpo è un'altra — e forzare il
colpo solo quando lo spostamento è dichiarato (tap), non quando è automatico.

---

## Cosa è stato verificato e va bene

Vale la pena dirlo, perché sono le cose che di solito sono rotte:

- **Nessun offset sistematico nella fase del decoder.** A jitter zero l'errore è
  0.3 ms a tutti i tempi provati. La geometria di `analysisSampleFor` (centro
  della finestra, hop, campioni persi dalla FIFO) torna.
- **L'anello di fase non dipende dal buffer.** Aggancio 2.85 s a 64 campioni e
  2.86 s a 1024, residuo 0.0014 contro 0.0018. Le costanti di tempo sono in
  secondi dappertutto, come da commento.
- **La griglia non torna mai indietro.** La correzione è sulla velocità, mai
  sulla posizione, e `effTempo = tempo · (1 − steer)` con steer limitato resta
  positivo: nessun impulso può essere emesso due volte o saltato.
- **Il seqlock dell'ipotesi è corretto**, fence in entrambe le direzioni e
  payload in parole atomiche.
- **L'ipotesi vecchia si estrapola giusta.** `songPhase` corregge con
  `samplesFed() − analysisSample`, quindi un blocco che non porta un'ipotesi
  nuova fa avanzare il bersaglio esattamente alla velocità giusta invece di
  puntare al passato.
- Le 122 asserzioni di `VPTests` passano, con la #3 corretta e il suo test
  stretto.

---

## Ordine consigliato

1. **#1 e #2 insieme** — sono lo stesso intervento (la fase da una media invece
   che da un picco) e coprono il difetto peggiore. Fatto quello, l'anello di fase
   può essere stretto, perché il rumore che gli 0.9 s di smoothing esistono per
   togliere non c'è più.
2. **#5.1** — snap a griglia libera quando non suona niente. Piccolo, senza
   rischi udibili, e si sente subito su START.
3. **#4** — il ri-aggancio sulla fase invece che sui BPM.
4. **#5.2** e **#6** — accordatura, dopo che i tre sopra hanno cambiato i numeri
   su cui si accorda.
