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

## Risultato misurato — 2 settembre 2026

Il banco finale del passaggio rapido è
`/tmp/virtualperc-vpalign-after.txt`, rigenerato dal `VPAlign` corrente in
Release con quattro seed fissi per caso. Il confronto semantico con
`/tmp/virtualperc-vpalign-before.txt` dà
`protected-pre-tempo-identical=True` e
`protected-post-tempo-identical=True`: aggancio, jitter, livello metrico,
deriva, buco, fase sulle rampe e ricalibrazione protetti sono invariati. Solo
l'autocontrollo dell'aggregatore e la sezione dedicata ai cambi di tempo sono
nuovi.

I numeri “prima” non hanno lo stesso criterio dei numeri finali: misuravano il
primo ingresso entro il 2 % che poi restava dentro; i finali misurano il primo
ingresso entro ±1 BPM e riportano il peggiore dei quattro seed. Non vanno
presentati come un confronto percentuale diretto:

- 118→124: prima 5,68 s (entro 2 % e stabile); finale 0,98 s (±1 BPM),
  fase al terzo battito 23,3 ms.
- 118→128: prima 6,81 s; finale 0,96 s, fase 23,9 ms.
- 128→120: prima 5,22 s; finale 1,02 s, fase 24,4 ms.
- 76→82 e 168→156 non erano misurati nel banco prima; nel finale sono
  rispettivamente 1,47 s / 23,7 ms e 0,78 s / 23,7 ms.

Tutti e cinque i gradini richiesti, fra 5,1 % e 7,9 %, hanno `resp ok 4/4`,
`fase ok 4/4`, una sola transizione per seed (`trans ok 4/4`), zero violazioni
degli impulsi e clock monotono `4/4`. L'adozione avviene al primo frame in cui
il secondo picco completa causalmente il secondo intervallo cambiato: non usa
frame futuri.

Il gradino di stress 100→140 è fuori da quell'inviluppo. Prima misurava
16,66 s col criterio del 2 %; il finale misura 26,70 s col criterio ±1 BPM e
57,1 ms al terzo battito, con zero transizioni rapide. Non è un miglioramento
misurato. Le rampe 118→126 in 4 s e 12 s restano fuori dal percorso rapido
(`trans ok 4/4` con zero transizioni attese), senza violazioni d'impulso e con
clock monotono; i loro valori finali visibili sono 0,00 s a ±1 BPM e
20,8 / 8,0 ms al terzo battito.

---

## In breve

| # | Cosa | Gravità | Stato |
|---|---|---|---|
| 1 | La fase usciva da **un solo picco**, il tempo da un fit su ventiquattro battiti | alta | **corretto** |
| 2 | Un aggancio a mezzo battito era **stabile**: si autoalimentava e non rientrava | alta | **corretto** |
| 3 | Il trim del tempo chiudeva **metà** dell'errore e si fermava lì | media | **corretto** |
| 4 | Il rilevatore di "l'analisi è andata altrove" **non poteva scattare** | media | **corretto** |
| 5 | Allineare uno scarto di fase costava 2–5 s (fino a 13 s in LOW) | media | **corretto** |
| 6 | Lo snap di fase all'ingresso in FOLLOWING non guardava quanto era grande | bassa | **corretto** |

Risultato complessivo, misurato:

| | prima | dopo |
|---|---|---|
| Rumore di fase in uscita dal decoder (22 ms di jitter in ingresso) | 22.2 ms rms | **8.7 ms rms** |
| Scatti di fase > 0.05 di battito, in 35 s | 11–49 | **0** |
| 168 BPM con ottavi a 0.45 | 168.00 BPM, **0.499 battiti fuori**, per sempre | 168.00 BPM, **0.000 fuori** |
| Chiusura di mezzo battito di scarto (HIGH / LOW) | 4.85 s / 13.34 s | **2.81 s / 5.92 s** |
| Chiusura a shaker fermo | uguale a sopra | **immediata** (la griglia si sposta) |
| Trim su una canzone 1 BPM lontana | si ferma a 0.500 | **1.000** |

`VPTests`: 128 asserzioni, 0 fallite. Le sei nuove sono state verificate
fallendo sul codice di prima.

---

## 1. La fase usciva da un solo picco

`BeatDecoder::observe` chiudeva così:

```cpp
const float newPeriod = 60.0f / std::max (kMinBpm, bpm);
if (lastBeatSec >= 0.0)
    phase = wrap01 ((timeSec - lastBeatSec) / newPeriod);
```

Il **periodo** arrivava da un fit ai minimi quadrati su ventiquattro battiti —
`fitPeriod` calcola pendenza *e* intercetta, e l'intercetta veniva buttata. La
**fase** arrivava da `lastBeatSec`, cioè dall'ultimo picco accettato: uno solo.
Il tempo era mediato su ventiquattro misure, la posizione del battito su una.

Il jitter di attacco del singolo battito passava dritto all'orologio, non
attenuato — e non come rumore continuo, ma a **scatti**, uno per ogni picco
accettato:

| tempo | jitter in ingresso | rms prima | rms dopo | scatti prima | dopo |
|---|---|---|---|---|---|
| 76 BPM | 22 ms | 22.2 ms | **8.7 ms** | 11 | **0** |
| 100 BPM | 22 ms | 0.0352 batt. | **0.0122** | 13 | **0** |
| 132 BPM | 22 ms | 22.5 ms | **7.6 ms** | 36 | **0** |
| 168 BPM | 22 ms | 0.0598 batt. | **0.0275** | 49 | **2** |

Senza jitter la fase era ed è esatta (0.0–0.3 ms): non c'era nessun errore
sistematico, la geometria di `analysisSampleFor` regge frame per frame.

**Corretto.** `fitPeriod` restituisce anche l'ancora — il tempo che la retta
predice per il battito più recente della sua finestra — e la fase si legge da
lì. Quale dei due fit la porta segue il regime, come già fa il tempo: un tempo
fisso può mediare su ventiquattro battiti, uno vivo no. L'ancora è presa
all'estremo recente della finestra e non al centro: il centro è l'estremo meglio
determinato di una retta, ma la fase serve *adesso*, e portarsi avanti dal
centro vuol dire estrapolare per mezza finestra con l'errore che ha il periodo.

Gli 0.9 s di smoothing di fase nell'orologio esistevano per ingoiare proprio
quegli scatti. Non ci sono più.

---

## 2. Mezzo battito fuori, e ci restava

Il caso peggiore, ed era **stabile**, non transitorio.

L'ancora della fase era l'ultimo picco accettato, e il cancello che decide cosa
è un picco (`kOnGridTolerance = 0.18`) misura rispetto a quell'ancora. Se
l'ancora finiva una volta su un ottavo, tutti i battiti veri cadevano a 0.5
dalla griglia e venivano scartati come controtempi — e gli ottavi, che cadevano
a 1.0, venivano accettati. L'errore si dava ragione da solo. Il cancello che
avrebbe dovuto salvarci (`kGridStaleBeats`, "se non atterra niente sulla griglia
allora la griglia è sbagliata") non scattava mai, perché sulla griglia sbagliata
atterrava tutto.

E non è materiale inventato: `docs/AI_BEAT_TRACKING.md` misura l'attivazione a
mezzo battito dal battito fra 0.73 e 0.77 su un mix a 76 BPM con ottavi pieni.
0.45 è dentro il normale.

| tempo | ottavo a | bpm trovato | errore di fase prima | dopo |
|---|---|---|---|---|
| 100 | 0.45 | 100.00 | 0.000 | 0.000 |
| 132 | 0.45 | 132.00 | 0.001 | 0.000 |
| **168** | **0.45** | **168.00** | **0.499 battiti** | **0.000** |

Nemmeno un fit lo risolve: i battiti su cui il fit lavora *sono* i controtempi.

**Corretto.** L'attivazione ripiegata sul periodo committato è l'unica misura
della catena che non passa dal cancello, e ripiegata sul periodo vero è alta sul
battito e piatta mezzo periodo dopo — che è esattamente la domanda "su quale
metà del battito siamo". `TempoEstimator::beatPhaseFor` la restituisce; il
decoder la confronta con la propria griglia a ogni battito e, se le due
discordano di più di un quinto di battito per tre battiti di fila, sposta
l'ancora e butta la storia dei battiti (che descriveva i controtempi).

Non viene usata per niente di più fine: otto bin sono un ottavo di battito, e la
precisione resta al fit. Ed è disattivata quando il ripiegamento stesso è piatto
a mezzo periodo (`contrast > 0.70`), cioè sul materiale dove il controtempo è
davvero forte quanto il battito e nessuno può distinguerli.

---

## 3. Il trim del tempo chiudeva metà dell'errore

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
| 20 s | trim 0.498 | trim 0.928 |
| 40 s | trim **0.500** | trim 0.999 |
| 80 s | trim **0.500** | trim **1.000** |

L'altra metà dell'errore di velocità restava addosso all'anello di fase come una
pendenza permanente, che mangiava margine di sterzata e teneva il BPM sullo
schermo mezzo punto lontano dal vero.

**Corretto**: il trim è un integratore, la misura è un incremento.

```cpp
tempoTrim = std::clamp (tempoTrim - measuredErrorBpm * trust, -3.5f, 3.5f);
```

Il polo passa da `1 - 2·trust` a `1 - trust`, quindi converge un po' più piano ed
è più stabile, non meno. Attivo sotto TAP e in regime `fixed`.

---

## 4. Il rilevatore di ricalibrazione non poteva scattare

`BeatTracker` aveva una via per accorgersi che l'analisi era finita su un'altra
canzone: se il BPM della rete si scostava dell'8% da `heldBpm` per 1.15 s netti,
entrava in `retuning` e all'uscita rimetteva la fase a posto e azzerava i voti
della battuta. Ma `heldBpm` è, cinque righe più in là, il tempo del follower —
che sta già inseguendo la rete con tau 0.55 s da agganciato.

| gradino da 100 BPM | sopra l'8% per | ricalibrava? |
|---|---|---|
| +15% | 0.31 s | no |
| +50% | 0.83 s | no |
| +75% | 0.97 s | no |
| −40% | 1.12 s | no |
| +100% / −50% | 0.00 s | no (l'ottava viene presa in un blocco) |

Nessuno scattava. `retuning` era codice morto, e con lui lo snap di fase per
canzone nuova, lo stato `RICALIBRO` e il ramo `retuning` di `canPlay`.

**Corretto, cambiando la grandezza misurata.** Qualunque cosa l'orologio sappia
misurare, l'orologio la sta anche chiudendo: un rilevatore che guarda l'errore
di fase viene battuto in velocità dall'anello esattamente come questo veniva
battuto sui BPM. Chi lo sa davvero è il decoder — lo dice quando butta via una
griglia. `BeatHypothesis::gridSerial` viene incrementato su un cambio di livello
metrico, su un ri-ancoraggio del ripiegamento e su un mezzo/doppio chiesto
dall'utente, e `BeatTracker` azzera i voti della battuta quando cambia. Quello è
l'unico momento in cui il conteggio della battuta non vale niente invece di
essere solo vecchio — e non scatta su un buco audio, che costa le prove recenti
ma non la canzone.

La macchina `retuning` è stata tolta: non si lascia in giro uno stato che non si
accende.

---

## 5. Quanto ci mette a mettersi sulla canzone

La fase si corregge cambiando velocità, mai spostando la griglia — è la scelta
giusta: una griglia spostata sotto una parte che suona raddoppia o salta un
colpo. Ma la sterzata era limitata al 3.5% (MEDIUM) / 5% (HIGH) del tempo
qualunque fosse l'errore, quindi il tempo di allineamento era `scarto / limite`
battiti e basta.

| segui | scarto 0.10 | 0.25 | 0.40 | 0.48 |
|---|---|---|---|---|
| HIGH | 1.79 s | 2.82 → **2.45** | 4.11 → **2.74** | 4.85 → **2.81** |
| MEDIUM | 2.30 s | 3.89 → **3.21** | 5.89 → **3.59** | 7.01 → **3.78** |
| LOW | 3.35 s | 7.04 → **4.95** | 11.14 → **5.65** | 13.34 → **5.92** |

**Corretto in tre punti.**

1. **Il tetto si apre con l'errore.** Il limite esiste per non far vivere
   l'anello contro il fondoscala sul rumore di fase dell'analisi, e quel rumore
   è centesimi di battito. Un quarto di battito non è rumore: è un'altra
   griglia. Sopra 0.06 di battito il tetto sale fino al 25% (HIGH), 18%
   (MEDIUM), 10% (LOW). Resta una velocità: `1 - steer` non si avvicina a zero,
   la griglia resta monotona, nessun colpo viene raddoppiato o perso — e il test
   lo verifica misurando il vuoto più lungo fra due impulsi durante il recupero
   (1.24 impulsi in HIGH, 1.10 in LOW).
2. **Un bersaglio lontano viene adottato in fretta.** Mediare su 0.9 s è giusto
   per centesimi di battito ed è sbagliato per un quarto. Se il bersaglio resta
   lontano più di 0.06 e con lo stesso segno per un quarto di secondo — due
   refresh dell'analisi, che il rumore non attraversa tenendo il segno — la
   costante di tempo si accorcia in proporzione a quanto è lontano.
3. **A shaker fermo la griglia si sposta e basta.** Il motivo per non spostarla
   è che c'è una parte sopra; quando non c'è, la correzione è gratis ed esatta.
   Questo copre l'attesa fra START e l'ingresso sull'uno — che prima era anzi
   *esclusa* dalla correzione di fase del tutto: per tutta l'attesa la fase non
   veniva corretta mai.

Il residuo si assesta a 0.012 di battito ovunque — è `kPhaseFloor`, voluto, e a
120 BPM sono 6 ms. Il tempo di aggancio non dipende dal buffer (2.67 s a 64
campioni contro 2.67 a 1024).

---

## 6. Lo snap all'ingresso in FOLLOWING non guardava quanto era grande

```cpp
else if (! tapHold && nnBpm > 50.0f && gridMuteSamples <= 0 && haveHyp)
    follower.snapPhase (songPhase);
```

Nessun filtro sulla grandezza dell'errore, e nessuna domanda su se lo shaker
stesse suonando — mentre il ramo gemello dieci righe sopra il filtro ce l'aveva
(`> 0.15f`). `snapPhase` alza `reanchor`, che **emette un impulso** sulla nuova
fase se sono passati più di `max(20 ms, mezzo impulso)` dall'ultimo. Per un tap
è quello che si vuole; per un rientro automatico in FOLLOWING in mezzo a un
pezzo è un colpo in più a 70 ms dal precedente, comprato per correggere un
centesimo di battito.

**Corretto**: a parte ferma si sposta sempre (è gratis), a parte che suona solo
sopra 0.12 di battito — sotto, l'anello lo chiude entro un battito e non si
sente niente.

---

## Cosa è stato verificato e va bene

- **Nessun offset sistematico nella fase del decoder.** A jitter zero l'errore è
  0.0–0.3 ms a tutti i tempi provati. La geometria di `analysisSampleFor`
  (centro della finestra, hop, campioni persi dalla FIFO) torna.
- **L'anello di fase non dipende dal buffer.** 2.67 s a 64 campioni e 2.67 a
  1024, residuo 0.0014 contro 0.0018.
- **La griglia non torna mai indietro.** La correzione è sulla velocità, mai
  sulla posizione, e questo resta vero anche con il tetto aperto al 25%.
- **Il seqlock dell'ipotesi è corretto**, fence in entrambe le direzioni e
  payload in parole atomiche.
- **L'ipotesi vecchia si estrapola giusta.** `songPhase` corregge con
  `samplesFed() − analysisSample`, quindi un blocco che non porta un'ipotesi
  nuova fa avanzare il bersaglio alla velocità giusta invece di puntare al
  passato.

## Seguito (28 agosto)

`VPAlign` ha due sezioni in più, e la seconda porta il banco fino in fondo:
guida `BeatDecoder` **e** `TempoFollower` insieme e misura la fase
dell'**orologio** contro la griglia scritta, che è il numero che si sente. Serve
per il passaggio senza batteria, e i risultati stanno in `docs/STATUS.md`. Due
cose riguardano direttamente questo documento:

- **Gli 0,9 s di lisciatura di fase.** La sezione 1 qui sopra chiude con «gli
  0,9 s esistevano per ingoiare proprio quegli scatti; non ci sono più», che si
  legge come un invito ad accorciarli — almeno su una mandata di linea, dove il
  percorso è uno solo e fermo. Misurato da un capo all'altro, **non è un
  miglioramento**: 0,35 s al posto di 0,90 costa 23,2 → 23,8 ms medi e
  52,4 → 54,1 al peggio attraverso un passaggio, e su un accelerando non cambia
  niente di misurabile. La fase del decoder è già in ritardo su un tempo che si
  muove, quindi mediarla di meno insegue quel ritardo più da vicino invece della
  band. Non spedito; la riga sta in `VPAlign` (`tau linea, senza prove`) e la
  nota in `Tracking/PhaseTrust.h`.
- **La costante adesso si apre da sola** quando i battiti che il fit sta
  agganciando sono piazzati peggio di quelli che *questo brano* stava dando, e
  con lei il pavimento della planata di velocità e un tetto sulla pendenza.
  Vedi `Tracking/PhaseTrust.h`.

## Cosa resta aperto

Un problema di **livello metrico**, non di fase, e fuori dal giro di questo
audit: con ottavi a 0.60 dell'ampiezza dei battiti l'estimatore può finire su un
sotto-armonico che non è nemmeno un'ottava (132 → 52.8, 168 → 67.2, cioè un
terzo). Il ripiegamento di fase non lo tocca — a livello sbagliato la fase non
vuol dire niente — e le righe corrispondenti in `VPAlign` sono marcate come
tali. È il prossimo posto dove guardare.
