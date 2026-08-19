# Percussioni perfette e intelligenti — cosa manca

Documento di gap analysis e piano di sviluppo. Risponde a una domanda sola:

> cosa serve perché le percussioni suonino **perfettamente** e **in modo intelligente**,
> seguendo sia un brano riprodotto sul dispositivo sia una band dal vivo, senza
> introdurre regressioni?

Non è una roadmap di prodotto (quella è `docs/ROADMAP.md`) e non è lo stato di
installazione (quello è `docs/STATUS.md`). Qui c'è **il delta fra il codice che
esiste oggi e il comportamento musicale voluto**, con riferimenti a file e riga,
severità, e per ogni voce il criterio di accettazione che la chiude.

Data: 19 agosto 2026 · albero: `0.1.0` · ~6.000 righe in `Source/`

---

## 0. Cosa vuol dire "perfetto" qui

Quattro requisiti misurabili. Tutto il resto del documento serve a questi.

| # | Requisito | Metrica di accettazione |
|---|---|---|
| R1 | **Tempo giusto** | BPM committed entro ±0.3 % del vero, ottava metrica corretta, per ≥ 10 minuti continui |
| R2 | **Fase giusta** | offset medio dei colpi rispetto al beat acustico entro ±10 ms, deviazione standard < 8 ms |
| R3 | **Metrica e accento giusti** | il "1" della battuta cade sul "1" del brano ≥ 95 % delle battute; il pattern non ruota mai da solo |
| R4 | **Musicalità** | la parte cambia con il brano (intensità, sezioni, stop, fill) invece di ripetere 8 step all'infinito |

Oggi l'albero copre **bene R1**, **in parte R2**, **poco R3**, **per niente R4**.

---

## 1. Mappa di cosa esiste (per capire i gap)

```
mic / ingresso USB
  └─ VirtualPercussionEngine::process        (audio thread)
       ├─ mixInputs            → mono unico
       ├─ subtractSpeakerLeak  → cancellazione rientro (SPEAKER)
       ├─ applyAnalysisMakeup  → gain statico verso il range di BeatNet
       ├─ NeuralBeatTracker.feed (FIFO SPSC)
       │     └─ worker: resample 22.05k → LogSpectFeatures 272-d
       │              → OnnxBeatModel (BeatNet LSTM) → BeatDecoder
       │              → BeatHypothesis {bpm, phase, regime, confidence}
       ├─ BeatTracker  → stato, TAP, compensazione latenza, snap downbeat
       ├─ TempoFollower → PLL, genera ClockTick (pulses + offset campione)
       └─ PercussionEngine::render → shaker + tumbao congas fisso
```

Il livello **analisi** (BeatNet + decoder + PLL) è maturo e ben documentato.
Il livello **performance musicale** (`PercussionEngine`, 419 righe) è ancora un
generatore di 8 step fissi. È lì che si concentra la distanza dal "suona in modo
intelligente".

---

## 2. Sintesi — le lacune, per severità

| ID | Area | Lacuna | Sev. | Requisito |
|---|---|---|---|---|
| **B1** | Percussioni | `1/16` produce ottavi, non sedicesimi | 🔴 bug | R4 |
| **B2** | Clock | `pulseBeatInBar` sbagliato per le suddivisioni dopo il wrap del beat | 🔴 bug | R3 |
| **B3** | Percussioni | voice stealing brutale → click sui campioni | 🔴 bug | R4 |
| **B4** | Analisi | `subtractSpeakerLeak` tratta solo i primi 2048 campioni → gradino nel segnale d'analisi | 🔴 bug | R1/R2 |
| **B5** | Analisi | drop della FIFO non resetta feature/decoder → onset fantasma | 🟠 | R1 |
| **B6** | Doc/codice | `AUTO` = ottavi nel codice, sedicesimi nella doc; "steal quietest" documentato ma non implementato | 🟡 | — |
| **M1** | Metrica | 4/4 cablato ovunque: niente 3/4, 6/8, 12/8 | 🔴 | R3 |
| **M2** | Metrica | nessun decoder di battuta: il "1" arriva da una soglia su un frame | 🔴 | R3 |
| **M3** | Feel | nessun rilevamento di swing/terzine: su uno shuffle lo shaker va contro | 🟠 | R4 |
| **M4** | Feel | nessuna scelta half-time / double-time | 🟠 | R4 |
| **P1** | Performance | pattern unico cablato, nessuna libreria di groove | 🔴 | R4 |
| **P2** | Performance | nessuna dinamica: velocity non segue né accenti né intensità del brano | 🔴 | R4 |
| **P3** | Performance | nessuna struttura: niente fill, niente stop, niente intro/outro | 🟠 | R4 |
| **P4** | Performance | continua a suonare nel silenzio e nei breakdown | 🟠 | R4 |
| **P5** | Suono | 4 take di shaker, 1 sola take per conga → effetto mitragliatrice | 🟠 | R4 |
| **P6** | Performance | `humanization` agisce solo sul volume, mai sul tempo | 🟡 | R4 |
| **I1** | Ingresso | su iPad non esiste presa digitale dell'audio di Spotify: solo microfono | 🔵 vincolo | R1 |
| **I2** | Ingresso | un solo bus mono; niente analisi per strumento, niente mid/side | 🟠 | R1 |
| **I3** | Ingresso | cancellazione del rientro a 1 tap e ritardo fisso | 🟠 | R1 |
| **I4** | Ingresso | AGC grezzo, nessun filtro passa-alto, nessuna calibrazione del rumore | 🟠 | R1 |
| **X1** | Real-time | seqlock senza fence di release in scrittura | 🟡 | robustezza |
| **X2** | Real-time | worker senza QoS/priorità: se scivola, la latenza cresce | 🟠 | R2 |
| **X3** | Real-time | blocchi più grandi di `maxBlock` vengono troncati, uscita non scritta | 🟡 | robustezza |
| **T1** | Test | nessuna CI; nessun test su metrica, pattern, leak, tenuta lunga | 🔴 | tutti |

Legenda: 🔴 blocca un requisito · 🟠 lo degrada · 🟡 debito · 🔵 vincolo di piattaforma.

---

## 3. Difetti già presenti nel codice

Questi vanno chiusi **prima** di aggiungere intelligenza: sono le regressioni che
altrimenti si portano dietro ogni funzione nuova.

### B1 — `1/16` suona come `1/8`

`Source/Percussion/PercussionEngine.cpp:354`

```cpp
else                       // groovePulses == 4 (sedicesimi)
{
    if ((idx & 1) != 0)    // scarta i sedicesimi dispari
        continue;
    step = ((barBeat * 2) + (idx / 2)) % 8;
}
```

Con `pulsesPerBeat = 4` gli impulsi dispari vengono saltati: premendo **1/16**
nell'interfaccia lo shaker continua a suonare ottavi. La griglia a 8 step del
tumbao è per ottavi e non è mai stata estesa a 16.

**Fix**: portare la tabella di pattern a 16 step per battuta e derivare lo step
da `barPulse` senza scartare impulsi. Il pattern delle congas resta sugli ottavi
scegliendo `Kind::shaker` (= silenzio conga) sui sedicesimi dispari.

**Accettazione**: un test che conta i colpi in 4 battute a 120 BPM → 16 con `1/4`,
32 con `1/8`, 64 con `1/16`, con spaziatura uniforme entro 1 campione.

### B2 — `pulseBeatInBar` sbagliato dopo il wrap del beat

`Source/Tracking/TempoFollower.cpp:308-317`

```cpp
int barBeat = beatAtStart;
if (tick.wrappedBeat && idx == 0)
    barBeat = beatInBar;      // nuovo beat: corretto
else if (tick.wrappedBeat && idx != 0)
    barBeat = beatAtStart;    // ← impulso DOPO il wrap, attribuito al beat vecchio
```

Se un blocco contiene sia il quarto sia l'ottavo successivo (buffer grandi, tempi
alti), l'ottavo riceve l'indice di battuta del beat precedente. `barPulse` finisce
sullo step sbagliato e il tumbao suona una conga fuori posto per un impulso.
Raro, ma è esattamente il tipo di errore che si sente e non si riesce a
riprodurre.

**Fix**: calcolare l'indice di battuta di ogni impulso dalla sua posizione
assoluta (`totalBeats` più la parte intera di `p / pulsesPerBeat`) invece che dai
due casi `beatAtStart` / `beatInBar`.

**Accettazione**: test che avanza il clock con blocchi da 64 a 4096 campioni a
60/120/180/210 BPM e verifica che `barPulse` sia **strettamente monotono modulo
la battuta**, senza ripetizioni né salti.

### B3 — voice stealing che produce click

`Source/Percussion/PercussionEngine.cpp:220-238`

```cpp
void PercussionEngine::stealKind (Kind kind) noexcept
{
    for (auto& v : voices)
        if (v.active && v.kind == kind)
            v.active = false;      // troncamento istantaneo
}
...
int slot = 0;                       // se nessuna voce è libera si sovrascrive la 0
```

Due problemi: la voce viene azzerata a metà campione (discontinuità → click), e
se tutte e 12 le voci sono occupate si sovrascrive comunque la voce 0, qualunque
cosa stia suonando. `docs/AUDIO_ENGINE.md` dichiara *"Up to 12 overlapping shaker
grains. Steal quietest"*: il codice non fa nessuna delle due cose.

**Fix**: rampa di release di 2–3 ms sulla voce rubata (contatore di fade nel
`Voice`), e scelta della vittima per ampiezza istantanea minima quando non ci sono
slot liberi.

**Accettazione**: test offline che rende 30 s a 200 BPM in `1/16` e verifica che
la derivata prima dell'uscita non superi mai una soglia (assenza di gradini), e
che `activeVoices()` non superi 12.

### B4 — la cancellazione del rientro spezza il segnale d'analisi

`Source/Audio/VirtualPercussionEngine.cpp:182-203`

```cpp
float hpBuf[2048];
const int n = std::min (numSamples, 2048);
...
for (int i = 0; i < n; ++i)
    mono[i] -= g * hpBuf[i];      // solo i primi n campioni
```

`prepare()` accetta `maxBlock` fino a `max(samplesPerBlockExpected * 2, 2048)`.
Quando il blocco supera 2048 campioni la sottrazione si ferma a metà: nel segnale
mandato a BeatNet compare un **gradino** a ogni callback. Un gradino è un onset
perfetto: genera attivazione spuria a cadenza fissa pari alla frequenza di
callback, cioè esattamente il tipo di falso pulse che il decoder è costruito per
non ignorare.

**Fix**: eliminare il buffer di stack fisso (allocare in `prepare()` alla
dimensione di `maxBlock`) e processare l'intero blocco.

**Accettazione**: test con blocchi da 4096 campioni in modo SPEAKER che verifica
l'assenza di discontinuità nel segnale d'analisi e che il BPM rimanga stabile.

### B5 — un overrun della FIFO lascia il decoder su un segnale discontinuo

`Source/AI/AudioFifo.h:83-86` conta i campioni persi e
`NeuralBeatTracker::analysisSampleFor` li usa per correggere il timestamp. Bene
per la latenza — ma **le feature no**: `LogSpectFeatures` e `BeatDecoder`
continuano attraverso il buco come se il segnale fosse continuo. Un salto di
audio è un transiente a banda larga: produce un picco di attivazione e un IOI
falso proprio nel momento in cui il sistema è già sotto carico.

**Fix**: propagare un flag "gap" dalla FIFO al worker; su gap, svuotare lo stato
delle feature, resettare lo stato LSTM del modello e marcare i beat successivi
come non utilizzabili per il fit finché la storia non è di nuovo continua.

**Accettazione**: test che forza un overrun a metà di un click a 120 BPM e
verifica che il BPM committed non si muova più dello 0.5 % e che non venga
registrato alcun beat fantasma.

### B6 — divergenze fra documentazione e codice

| Doc | Codice | Nota |
|---|---|---|
| `TD-10`: "AUTO mappa sui sedicesimi" | `BeatTracker.cpp:404` → ottavi; `VirtualPercussionEngine.cpp:287` → 2 impulsi | scegliere uno dei due e allineare |
| `AUDIO_ENGINE.md`: "Steal quietest" | `stealKind` uccide tutte le voci dello stesso tipo | vedi B3 |
| `ARCHITECTURE.md`: `GrooveEngine` come modulo | non esiste nell'albero | va creato (§5) o tolto dalla doc |

Sono innocue oggi, velenose fra sei mesi: la doc è la specifica con cui si
giudicano le regressioni.

---

## 4. Cosa manca all'ascolto

### I1 — Su iPad non esiste una presa digitale dell'audio di Spotify (vincolo)

Va detto senza ambiguità perché condiziona tutto il resto: **iPadOS non espone
nessuna API pubblica per catturare l'audio di un'altra app**. `SPEAKER` funziona
via microfono e questo è l'unico percorso possibile per Spotify, Apple Music,
YouTube. Ogni miglioramento di R1/R2 su quel percorso passa per l'acustica della
stanza, non per il software.

Esistono tre vie per ottenere il segnale digitale, tutte legittime e tutte da
implementare, non da aspettare:

| Via | Cosa serve | Cosa si ottiene |
|---|---|---|
| **Riproduzione interna** | import file da Files/AirDrop + `AVAudioPlayerNode` nel nostro grafo | analisi sul PCM puro: R1/R2 perfetti, zero rientro, latenza nota |
| **Libreria musicale locale** | `MPMediaQuery` + `assetURL` (solo brani non DRM) | come sopra, per la musica dell'utente |
| **Ingresso digitale** | interfaccia USB-C, brano da un secondo dispositivo | come sopra, e già supportato dal percorso KIT MIC |

La prima è la più utile e la meno costosa: un `AVAudioPlayerNode` che scrive nello
stesso `mono` d'analisi rende il caso "seguo un brano" **deterministico e
testabile**, e diventa il banco di prova offline di tutto il resto del documento.
Con essa il modo SPEAKER resta ciò che è: il caso difficile, non l'unico caso.

### I2 — Un solo bus mono

`VirtualPercussionEngine::mixInputs` (riga 92) somma e media i canali. Perde due
cose: la separazione per strumento prevista da `docs/ARCHITECTURE.md` (kick /
snare / hat: MVP 2) e l'informazione stereo. Con un mic stereo o due mic il
**mid/side** darebbe gratis un canale in cui il rientro dello shaker, quasi
centrato, è attenuato.

**Da implementare**: `ChannelAssignment` reale (già tipizzato nella doc, assente
nel codice), somma pesata invece che media, opzione mid/side per l'analisi.

### I3 — Cancellazione del rientro a un solo tap

`subtractSpeakerLeak` stima un **guadagno scalare** su un **ritardo fisso**
derivato dalla latenza dichiarata dal device. Non è un canceller: la risposta
della stanza è multi-tap e il ritardo reale non è quello dichiarato. Al volume
basso funziona; alzando lo shaker l'app rischia di inseguire sé stessa — che è
proprio il fallimento peggiore, perché è stabile e sembra un lock.

**Da implementare**: NLMS a 128–512 tap sul dominio dell'inviluppo (non del
segnale: serve a togliere gli onset, non l'audio), stima del ritardo per
correlazione all'avvio, congelamento dell'adattamento quando l'ingresso è forte
(double-talk). Riferimento nel ring già esistente (`outRing`), ma **preso dopo il
master volume**, che oggi non lo è (riga 321-326: il ring riceve il segnale
pre-master mentre l'altoparlante riproduce quello post-master), e includendo il
monitor del CLICK TEST, oggi escluso.

**Accettazione**: test che riproduce shaker + silenzio d'ingresso e verifica che
il tracker **non** raggiunga mai `FOLLOWING` (nessun auto-lock), a volume master
0.2, 0.6 e 1.0.

### I4 — Condizionamento del segnale d'analisi

`applyAnalysisMakeup` (riga 206) applica un guadagno statico ricavato
dall'inviluppo di picco, con una finestra utile `0.0005 … 0.22`. Manca:

- **passa-alto** a ~30 Hz: il rumore di maneggio e il rumble della stanza entrano
  dritti nel primo filtro della filterbank;
- **AGC con attacco/rilascio musicali** invece di un guadagno di picco: BeatNet è
  addestrato su materiale normalizzato, e il fattore attuale può variare di 16×
  fra due blocchi vicini alle soglie;
- **calibrazione del rumore di fondo** all'avvio, per distinguere "stanza
  silenziosa" da "musica bassissima" — oggi la distinzione è una soglia fissa
  (`0.0010` / `0.0020`, `BeatTracker.cpp:319`).

---

## 5. Cosa manca alla comprensione musicale

È qui che sta la differenza fra "sta a tempo" e "suona in modo intelligente".

### M1 — La metrica è cablata a 4/4

Il quattro compare come costante in almeno cinque punti indipendenti:

| Punto | Riga |
|---|---|
| `barPhase() = (beatInBar + phase) * 0.25` | `TempoFollower.h:47` |
| `beatInBar = (beatInBar + crossed) & 3` | `TempoFollower.cpp:285` |
| `snapBeat`: `beatIndex % 4` | `TempoFollower.cpp:130` |
| `beatsInBar = (beatsInBar + 1) % 4` | `BeatDecoder.cpp:571` |
| `hyp.barPhase = (beatsInBar + phase) * 0.25f` | `BeatDecoder.cpp:594` |
| tabella tumbao a 8 step = 4/4 in ottavi | `PercussionEngine.cpp:363` |

Su un brano in 3/4 o 6/8 il clock accumula un beat di scarto ogni battuta: il
tumbao ruota lentamente e la parte diventa insensata pur restando "a tempo".

**Da implementare**: un tipo `TimeSignature { beatsPerBar, beatUnit }` propagato
da `BeatDecoder` a `TempoFollower` a `PercussionEngine`, con
`beatsPerBar` stimato dal periodo dell'attivazione di downbeat (autocorrelazione
sulla serie di downbeat, candidati 2/3/4/6), più override manuale
nell'interfaccia. Nessuna costante `4` deve sopravvivere fuori da un default.

**Accettazione**: test su attivazioni sintetiche in 3/4 e 6/8 — `barPhase` torna a
zero ogni 3 (risp. 6) beat, e il pattern non ruota in 64 battute.

### M2 — Non esiste un decoder di battuta

Il "1" oggi nasce da una soglia su un singolo frame:

```cpp
// BeatDecoder.cpp:572
if (prevDownbeat > downThresh)      // 0.40
{
    lastDownbeatSec = eventTimeSec;
    beatsInBar = 0;                 // la battuta riparte da qui, sempre
}
```

e a valle `BeatTracker` accetta lo snap solo in una finestra stretta:

```cpp
// BeatTracker.cpp:411
if (hadDownbeat && ... && follower.beatPhase() < 0.22f
    && follower.beatInBarIndex() != 0)
    follower.snapDownbeat (follower.beatPhase());
```

Due conseguenze reali: un singolo frame sopra soglia sul beat 3 sposta la battuta
in modo permanente; e se i downbeat veri arrivano sistematicamente con fase ≥ 0.22
(succede quando la fase del clock è leggermente in anticipo) **la battuta non si
allinea mai**. In entrambi i casi R3 fallisce mentre R1 e R2 sembrano perfetti —
è il difetto più difficile da diagnosticare dall'ascolto, perché "va a tempo".

BeatNet risolve questo con il filtro a particelle sullo stato di battuta; noi
abbiamo deliberatamente scelto un decoder deterministico (`TD-05`), quindi serve
l'equivalente deterministico:

**Da implementare**: un **istogramma circolare di downbeat** su `beatsPerBar`
posizioni. Ogni beat aggiunge `pDownbeat` al bin corrispondente, con decadimento
esponenziale su ~8 battute. La battuta si allinea al bin vincente **solo** quando
supera il secondo di un margine stabilito e per N battute consecutive. Rotazione
dell'indice tramite `rotateBarIndex` (già presente,
`TempoFollower.cpp:133`), mai `snapDownbeat`, così la fase del beat non viene
toccata quando si corregge soltanto la battuta.

**Accettazione**: test con downbeat corretti al 70 % e 30 % di falsi su beat 2/3/4
→ la battuta si allinea entro 8 battute e non ruota nelle 56 successive.

### M3 — Nessun rilevamento di swing

La griglia è rigidamente isocrona (`TempoFollower::advance` divide il beat in
`pulsesPerBeat` parti uguali). Su uno shuffle o su un blues in 12/8 gli ottavi
dello shaker cadono esattamente dove la musica non ha nulla: è la differenza fra
"accompagna" e "disturba".

**Da implementare**: stima del **rapporto di swing** dalla distribuzione delle
attivazioni fra i beat (bimodale a 0.5 → dritto; a 0.66 → terzinato), e una
posizione di impulso non uniforme in `advance()`: `pulsePhase[i]` da una tabella
invece che `i / pulsesPerBeat`.

**Accettazione**: attivazioni sintetiche con swing 50 %, 58 %, 66 % → rapporto
stimato entro ±0.02 e impulsi resi alle posizioni corrispondenti.

### M4 — Nessuna scelta di half-time / double-time

`TempoEstimator` sceglie l'ottava metrica **del segnale**, correttamente. Ma la
scelta **musicale** è un'altra cosa: a 170 BPM un percussionista suona in
half-time, a 65 BPM raddoppia. Oggi lo shaker fa ottavi a 170 BPM, cioè 5.7 colpi
al secondo, ininterrottamente.

**Da implementare**: mappatura `bpm → densità di griglia` nel livello di
performance (non nel clock: il clock resta sul pulse vero), con isteresi e
override manuale. È il vero significato che dovrebbe avere `Subdivision::autoDetect`,
oggi un alias di "ottavi" (`BeatTracker.cpp:404`).

---

## 6. Cosa manca alla resa percussiva

`PercussionEngine` è oggi un generatore di pattern fisso. Serve un livello sopra:
il `GrooveEngine` che `docs/ARCHITECTURE.md` già nomina e che non esiste
nell'albero.

### P1 — Nessuna libreria di groove

```cpp
// PercussionEngine.cpp:363 — l'intero repertorio dell'app
static constexpr Kind kConga[8] = {
    Kind::tumba, Kind::shaker, Kind::open, Kind::slap,
    Kind::open,  Kind::shaker, Kind::slap, Kind::open
};
```

Un tumbao, sempre, per ogni brano, in ogni stile, a ogni tempo.

**Da implementare**: un formato di pattern dichiarativo — passi per battuta,
strumento, velocity, probabilità, condizione di battuta (`ogni 4ª`, `solo
finale`) — e una libreria iniziale di 6–8 groove (tumbao, marcha, shaker dritto,
shaker 16, songo, bossa, 6/8 afro, shuffle). Selezione automatica da tempo e
metrica, override manuale. Il rendering resta dov'è: `PercussionEngine` riceve
eventi già decisi, non decide.

Questa separazione è la condizione per non introdurre regressioni: il motore di
resa audio, che oggi funziona, non cambia più; cambia solo chi gli dice cosa
suonare.

### P2 — Nessuna dinamica

`triggerShaker` (`PercussionEngine.cpp:229-244`):

```cpp
const float vel = 0.78f + 0.14f * (1.0f - hum) + 0.08f * hum * rng.nextFloat();
```

La velocity non dipende da: posizione nella battuta (niente accento sul 1),
posizione nel beat (niente distinzione forte/debole), intensità del brano.

**Da implementare**: velocity = `accento di pattern × curva di battuta ×
inviluppo di intensità del brano`, dove l'intensità viene da una misura già
disponibile a costo zero sul worker (energia delle feature log-spect, media su
~1 s). Un percussionista che non abbassa il volume nella strofa non è un
percussionista.

**Accettazione**: test su un brano sintetico con strofa a −12 dB → la velocity
media dello shaker segue entro ±20 % la dinamica dell'ingresso, con costante di
tempo ≥ 1 s (nessun pumping).

### P3 — Nessuna consapevolezza della struttura

Niente fill in fondo alle 4/8 battute, niente ingresso graduale, niente
riconoscimento di ritornello. Serve poco per un salto qualitativo grosso: il
contatore di battute esiste già (`TempoFollower::beatsElapsed`), l'intensità la
introduce P2. Con quei due si ottengono fill condizionati e variazione per
sezione senza alcun modello nuovo.

### P4 — Continua a suonare nel silenzio

`BeatTracker.cpp:567`:

```cpp
const bool canPlay = armed
                  && (currentState == TrackingState::following
                      || currentState == TrackingState::lowConfidence
                      || currentState == TrackingState::recovering
                      || retuning);
```

`lowConfidence` e `recovering` sono inclusi di proposito — "se il tracking cala,
la percussione continua all'ultimo tempo attendibile" (`docs/ARCHITECTURE.md`) —
e per un buco di due battute è la scelta giusta. Ma `heldBpm` non torna mai a zero
dopo il primo lock (riga 499: `if (lockedOnce && ...) heldBpm = currentTempo()`),
quindi **se il brano finisce lo shaker continua per sempre**.

**Da implementare**: distinzione fra *dropout* (< 2 battute → si tiene) e *fine /
breakdown* (silenzio > 2 s → dissolvenza musicale su una battuta e attesa del
rientro sul downbeat successivo). Non un mute secco: una chiusura.

**Accettazione**: test che toglie il segnale dopo 30 s → la percussione sfuma
entro 2 battute e riparte sul downbeat quando il brano rientra, senza passare da
START.

### P5 — Varietà timbrica insufficiente

`kTakes = 4` per lo shaker; **una sola** take per tumba, open e slap
(`synthesizeCongas`, riga 178). Alla stessa velocity, lo stesso identico campione:
a 120 BPM in ottavi sono 4 colpi di conga identici al secondo.

**Da implementare**: 6–8 take per strumento, selezione round-robin con esclusione
dell'ultima usata, e micro-variazione di intonazione (±15 cent) e di inviluppo per
colpo. Costo: memoria, calcolata una volta in `prepare()`; nessun impatto
sull'audio thread.

### P6 — `humanization` non tocca il tempo

Il parametro esiste, è cablato dall'interfaccia, ed è **solo** una modulazione di
volume. L'umanizzazione percepita è per la maggior parte **timing**: ±3–8 ms con
distribuzione correlata (non rumore bianco), e un microtiming sistematico per
posizione di griglia (il classico "dietro" sul backbeat).

**Da implementare**: offset temporale per colpo nel `GrooveEngine`, applicato
all'offset di campione già passato a `triggerShaker`. L'infrastruttura c'è
(`sampleOffset` è campione-esatto); manca solo chi lo perturba.

---

## 7. Performance e robustezza real-time

### X1 — Il seqlock non ha la fence di release in scrittura

`Source/AI/BeatHypothesis.h:59-63`:

```cpp
const uint32_t s = seq.load (std::memory_order_relaxed);
seq.store (s + 1u, std::memory_order_release);   // marcatore dispari
value = h;                                        // ← può essere spostata prima
seq.store (s + 2u, std::memory_order_release);
```

Una store *release* impedisce alle operazioni **precedenti** di scivolare dopo; non
impedisce a quelle **successive** di risalire prima. Formalmente il lettore può
osservare `seq` pari con `value` scritto a metà. Serve `seq.store(s+1, relaxed)`
seguito da `std::atomic_thread_fence(std::memory_order_release)`. Il `value`
non-atomico letto durante la scrittura resta UB formale: è la pratica standard dei
seqlock, ma va annotato esplicitamente.

Basso rischio in pratica, costo del fix vicino a zero, e riguarda l'unico canale
fra analisi e clock.

### X2 — Il worker non ha priorità

`NeuralBeatTracker::start` lancia uno `std::thread` con priorità di default
(`NeuralBeatTracker.cpp:54`) che fa polling a 1 ms. Sotto carico iOS lo può far
scivolare: la latenza della pipeline cresce, `leadMs` la misura correttamente ma
la correzione è retroattiva. Va impostata la QoS *user-initiated* (non
*user-interactive*, che è per l'audio) e sostituito il polling con una condition
variable, così il thread si sveglia quando c'è audio invece che 1000 volte al
secondo.

**Accettazione**: strumentare il percentile 95 di `leadMs` durante un test con
carico CPU artificiale; deve restare sotto 120 ms.

### X3 — Blocchi più grandi di `maxBlock`

`VirtualPercussionEngine.cpp:243` tronca `numSamples` a `maxBlock`, ma i
canali di uscita vengono scritti solo per `numSamples` campioni: la coda del
buffer conserva ciò che c'era. Non succede con `prepare()` chiamata come oggi, ma
può succedere su un cambio di route prima che `prepareToPlay` sia rieseguita.
Va azzerata esplicitamente la coda del buffer d'uscita.

---

## 8. Test e CI — la parte che impedisce le regressioni

Oggi: 30 test host che passano, `Tests/TestMain.cpp` + `Tests/TestAiBeat.cpp`,
**nessuna CI** (`.github/` non esiste). La copertura è buona su decoder, PLL, TAP,
FIFO e TSM. È **assente** esattamente sulle aree di questo documento:

| Area | Coperta oggi | Da aggiungere |
|---|---|---|
| Clock e PLL | ✅ | test su blocchi variabili (B2) |
| Decoder BPM/regime | ✅ | 3/4 e 6/8 (M1); metrica dopo il cambio di brano |
| Downbeat / battuta | ❌ | allineamento e non-rotazione (M2) |
| Pattern e griglia | ❌ | conteggio colpi per suddivisione (B1), microtiming (P6) |
| Rientro speaker | ❌ | nessun auto-lock sul proprio shaker (I3) |
| Tenuta lunga | ❌ | 10 minuti di click: deriva di fase cumulata < 15 ms |
| Cambi di device | ❌ | 44.1 ↔ 48 kHz, buffer 64…4096, riesecuzione di `prepare` |
| Qualità audio | ❌ | assenza di click (B3), assenza di clipping |

**Da implementare per prima cosa**: una CI su macOS che esegua
`./scripts/run-tests.sh` a ogni push. Senza CI, "senza regressioni" non è
verificabile — è una speranza.

Aggiungere inoltre un **banco di prova offline riproducibile**: `process()` accetta
già buffer arbitrari (`docs/TEST_PLAN.md`), quindi serve solo un piccolo corpus
(click, kit, brano completo, shuffle, 3/4, brano con stop) e una metrica unica —
offset medio e deviazione dei colpi resi rispetto ai beat annotati. Quello è il
numero che dice se una modifica ha migliorato o peggiorato l'app, e va stampato a
ogni build.

---

## 9. Ordine di implementazione

Ogni fase è indipendente, ha un criterio d'uscita verificabile e non può rompere
la precedente. Questo ordine è la parte non negoziabile del piano: mette per prime
le cose che rendono misurabile tutto il resto.

### Fase 0 — Rendere verificabile (nessun cambiamento funzionale)
1. CI su macOS con `run-tests.sh`.
2. Banco di prova offline + metrica di offset/deviazione stampata a ogni build.
3. Allineare doc e codice (B6).

*Uscita*: ogni push produce un numero confrontabile con quello precedente.

### Fase 1 — Chiudere i difetti (nessuna funzione nuova)
B4 → B2 → B1 → B3 → B5 → X1 → X3.

*Uscita*: 30 test esistenti verdi, più i nuovi test di ciascun punto; la metrica di
Fase 0 non peggiora.

### Fase 2 — Ingresso affidabile
I1 (riproduzione interna del file), I4 (condizionamento), I3 (NLMS), I2 (mid/side).

*Uscita*: sul percorso file l'offset medio è < 5 ms; in SPEAKER nessun auto-lock a
volume pieno.

### Fase 3 — Metrica e battuta
M1 (`TimeSignature`) → M2 (istogramma di downbeat).

*Uscita*: 4/4, 3/4, 6/8 riconosciuti; il "1" allineato in ≥ 95 % delle battute sul
corpus; nessuna rotazione in 64 battute.

### Fase 4 — Livello musicale
Introduzione di `GrooveEngine` (P1) → dinamica (P2) → gestione silenzio/stop (P4)
→ struttura e fill (P3) → half/double-time (M4) → swing (M3).

*Uscita*: sul corpus la parte cambia in modo udibile fra sezioni; R4 soddisfatto.

### Fase 5 — Rifinitura del suono
P5 (take multiple) → P6 (microtiming) → X2 (QoS del worker).

*Uscita*: nessuna ripetizione timbrica percepibile; p95 di `leadMs` < 120 ms sotto
carico.

---

## 10. Metodo, perché "senza regressioni" sia vero

Cinque regole, tutte già rispettate dall'albero attuale: vanno mantenute quando il
codice cresce.

1. **Il clock resta sull'audio thread, l'intelligenza no.** Scelta di pattern,
   sezione, intensità e swing si decidono sul worker o in `prepare()`, e arrivano
   al thread audio come dati già pronti. Nessuna allocazione, nessun lock, nessun
   ramo costoso in `render()`.
2. **Un livello nuovo, non un livello modificato.** `GrooveEngine` si inserisce
   *sopra* `PercussionEngine`; il motore di resa non cambia. È la ragione per cui
   la Fase 4 non può far regredire le Fasi 1–3.
3. **Ogni fix arriva con il test che lo avrebbe intercettato.** Vale in
   particolare per B1–B5: sono tutti difetti che un test avrebbe visto.
4. **Nessuna costante musicale sparsa.** `4`, `0.25`, `8 step`: ognuna va sostituita
   da un parametro, altrimenti M1 va rifatto a ogni funzione nuova.
5. **La metrica di Fase 0 è il giudice.** Una modifica che la peggiora non entra,
   qualunque cosa sembri all'ascolto in cuffia.

---

## 11. Riepilogo in una riga

L'app **sente** già bene: BeatNet, il decoder a tre sorgenti e il PLL sono la parte
difficile ed è fatta. Quello che manca è **capire la battuta** (M1, M2) e
**suonare come un percussionista invece che come una drum machine** (P1–P6), su
una base di ingresso più solida (I1–I4) e dopo aver chiuso cinque difetti che oggi
possono sabotare qualunque miglioramento (B1–B5). Con la CI e il banco di prova
della Fase 0, tutto questo è sviluppo misurabile invece che sviluppo a orecchio.
