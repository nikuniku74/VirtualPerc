# Loop registrati — il percussionista che suona davvero

Documento operativo per il nuovo motore a loop registrati. Dice cosa c'è nel
codice, com'è fatto il formato, e — la parte che serve a te — **quali loop
vanno registrati e come vanno consegnati**.

Stato: motore completo e testato, **libreria audio assente**. Il flag di build è
`VP_ENABLE_RECORDED_LOOPS` ed è **spento**. Con il flag spento l'app è
esattamente quella di prima: suona `PercussionEngine` e nient'altro.

---

## 1. Cosa è stato aggiunto

| File | Cosa fa |
|---|---|
| `Source/Loops/LoopManifest.*` | Il formato: cosa si sa di una performance registrata |
| `Source/Loops/WavFile.*` | Lettore WAV senza JUCE, fuori dal thread audio |
| `Source/Loops/LoopBank.*` | Tutta la libreria in memoria, più la regola di scelta |
| `Source/Loops/LoopPlayer.*` | Il player agganciato al `ClockTick` |
| `Source/Loops/HybridPercussionRenderer.*` | Chi dei due suona, e come avviene il passaggio |
| `Source/Stretch/LoopStretcher.*` | Signalsmith Stretch (o WSOLA di riserva) |
| `Tests/TestLoops.cpp` | I test di accettazione |

Non è stato toccato niente di `BeatDecoder`, `BeatTracker`, `TempoFollower`.
`PercussionEngine` non è stato modificato: è pilotato, non sostituito, e resta
il fallback sempre disponibile.

`Source/Stretch/TimeStretchEngine.*` resta com'è — è il prototipo, e
`loadPercussionLoop` continua a usarlo. Il nuovo motore non lo tocca.

## 2. Come funziona, in breve

- La posizione musicale viene **presa dal `ClockTick`**: integrata fra un
  quarto e l'altro, e **piazzata esattamente** su ogni quarto che l'orologio
  emette. È l'aggancio di fase: la battuta uno del loop e la battuta uno del
  brano sono lo stesso campione.
- Quella posizione passa per i **marker** della registrazione e diventa una
  posizione nel file.
- Lo stretcher restituisce esattamente un blocco. La lettura della sorgente
  viene corretta **per velocità, mai con un salto** — lo stesso principio con
  cui `TempoFollower` corregge la fase — quindi nessun colpo può essere
  raddoppiato o perso.
- La **latenza dello stretcher è misurata**, non presa dai numeri della
  libreria: in `prepare()` un colpo secco passa per il backend a due rapporti
  diversi e si guarda dove esce. La lettura della sorgente corre avanti di
  quella quantità, altrimenti ogni colpo arriverebbe tardi di 120 ms.
- **Congas e shaker sono due player**, uno per stem, bilanciati con la stessa
  curva che `PercussionEngine` usa su `instrumentMix`. La parte passa alla
  registrazione solo se *ogni* strumento acceso ne ha una: mezza parte registrata
  e mezza a colpi singoli sono due percussionisti, non uno.
- Un loop tagliato bene, avvolto su sé stesso, **è un segnale continuo**: la
  lettura semplicemente gira, lo stretcher non vede nessuna discontinuità, e
  quindi non c'è niente da sfumare e niente che possa cliccare. Il crossfade
  serve solo per il **cambio di registrazione**, ed è fatto con due stretcher.
- Lo **swing** viene aggiunto in avanti (feed-forward), non lasciato scoprire
  all'anello di correzione: l'anello ha una costante di tempo più lunga di un
  quarto, e uno spostamento dentro il quarto passandoci dentro usciva a metà.

### La regola automatica

| Regime (dal decoder) | Chi suona | Perché |
|---|---|---|
| `fixed` | la registrazione | è un click: va corretta solo la deriva, molto lentamente |
| `live` | colpi singoli | il tempo si muove: i colpi lo seguono liberamente |
| `unknown` | colpi singoli | il motore attuale, finché il tempo non si assesta |

Il passaggio avviene **solo su un quarto**, di preferenza a inizio battuta, con
un crossfade di 45 ms, e **non tocca né l'orologio né la frase**. Il regime deve
tenere la sua risposta per ~24 blocchi prima che il passaggio avvenga, così un
tremolio del decoder non produce due scambi al minuto.

### Cosa resta ai colpi singoli

Entrata e uscita, cambi di sezione, riduzione dinamica, e tutte le correzioni
durante accelerando e rallentando — perché in quei momenti è il motore a colpi
singoli che suona. Fill e variazione B sono presi **dalla libreria** quando c'è
la registrazione (ruoli `fill` e `grooveB`, sulla frase di otto battute), e si
ripiega su `grooveA` quando non c'è.

Lo strato di accenti *sopra* una registrazione esiste
(`setAccentLayer`) ma è **a zero di default**, e deliberatamente: sovrapporre un
colpo sintetico a un groove registrato è una decisione da prendere con il
materiale davanti, non prima.

## 3. Il formato del manifest

Un file di testo per banco, `bank.vploops`, accanto ai WAV. Non JSON: si scrive
a mano quanto si genera, si legge una volta sola fuori dal thread audio, e un
formato a righe sbaglia sulla riga sbagliata invece che tre sezioni dopo.

```
# banco dance
version 1
bank dance

[loop]
id          dance_grooveA_120_straight_congas_t1
file        dance_grooveA_120_straight_congas_t1.wav
style       dance            # marcha rock dance pop samba funk reggae bossa twoone
role        grooveA          # grooveA | grooveB | fill | intro
stem        congas           # congas | shaker
bpm         120.0
bars        2
meter       4/4
firstBeatSample 0
frames      192000
channels    2
sampleRate  48000
intensity   0.5              # 0..1, quanto è stata suonata forte
swing       0.0              # 0 = dritto, 1 = terzina piena
take        1
beats       0 24000 48000 72000 96000 120000 144000 168000
```

- `firstBeatSample` **non si assume zero.** Una battuta esportata da un DAW
  porta quasi sempre qualche campione di pre-attacco prima della stanghetta, e
  un file tagliato duro a campione zero ha perso quel pre-attacco: si sente come
  un clic a ogni giro. L'export onesto tiene il pre-roll e dice qui dov'è il
  quarto.
- `beats` è facoltativo: senza, i quarti si assumono equispaziati a `bpm`. Con,
  deve avere un marker per quarto (o uno in più per la fine del corpo), e il
  primo deve valere `firstBeatSample`.
- `swing` è lo swing **che c'è nella registrazione**, non quello che l'utente
  chiede. La stessa mappa di `GrooveEngine`: la seconda croma sta a
  `0.5 + swing/6` del quarto.

`scripts/make_loop_manifest.py` genera il manifest da una cartella di WAV e un
CSV, e ricontrolla che i file esistano e siano leggibili.

## 4. Cosa devi registrare e consegnare — la lista

**Sì, i loop li devi fornire tu.** Non ci sono nel repository e non possono
essere sintetizzati: è esattamente il materiale che il motore esiste per
suonare. Il codice è pronto, testato e spento; si accende con la libreria.

### Prima consegna — solo DANCE, per il confronto d'ascolto

Come hai detto tu: un solo stile, due stem, pochi loop. Serve a decidere se la
strada è quella, prima di registrare tutto il resto.

| # | role | stem | BPM | swing | intensity | take | bars |
|---|---|---|---|---|---|---|---|
| 1 | grooveA | congas | 110 | 0.0 | 0.5 | 1 | 2 |
| 2 | grooveA | congas | 110 | 0.0 | 0.5 | 2 | 2 |
| 3 | grooveA | congas | 124 | 0.0 | 0.5 | 1 | 2 |
| 4 | grooveA | congas | 124 | 0.0 | 0.5 | 2 | 2 |
| 5 | grooveA | congas | 138 | 0.0 | 0.5 | 1 | 2 |
| 6 | grooveA | congas | 138 | 0.0 | 0.5 | 2 | 2 |
| 7–12 | grooveA | **shaker** | 110 / 124 / 138 | 0.0 | 0.5 | 1 e 2 | 2 |

Sono **12 file**. Tre BPM nativi coprono da 98 a 155 BPM con il limite di
stretch a ±12 %, che è tutto il dance. Due take per BPM perché una parte che si
ripete identica per quattro minuti si sente che è una macchina, per quanto sia
suonata bene.

### Seconda consegna, se l'ascolto convince

| # | role | stem | BPM | note |
|---|---|---|---|---|
| 13–18 | grooveB | congas + shaker | 110 / 124 / 138 | la variazione, un take basta |
| 19–24 | fill | congas + shaker | 110 / 124 / 138 | **una battuta**, non due |
| 25–30 | grooveA | congas + shaker | 110 / 124 / 138 | `intensity 0.85`, la versione tirata |
| 31–36 | grooveA | congas + shaker | 110 / 124 / 138 | `swing 0.6`, la versione shuffle |

Poi, e solo poi, gli altri stili con la stessa griglia.

### Requisiti audio — non negoziabili

- **WAV, 48 000 Hz.** Non 44,1: ogni marker del manifest è una posizione in
  campioni, e un file a 44,1 descritto a 48 mette ogni quarto all'8,8 % di
  distanza da dove dice di essere. Il caricatore lo rifiuta, non lo converte.
- 24 bit va benissimo (anche 16 o float 32).
- Mono o stereo. Stereo è meglio per le congas.
- **Loop tagliati esatti.** La lunghezza del corpo deve essere esattamente
  `battute × 4 × 60/BPM × 48000` campioni. Suonati a click, esportati a
  stanghetta.
- **Nessun riverbero stampato**, nessun delay, nessuna coda. Il riverbero lo
  mette l'app (`EngineSettings::reverbAmount`), e una coda stampata si
  sovrappone all'inizio del giro successivo.
- **Congas e shaker su file separati.** Un file stereo con dentro tutti e due
  non si può più bilanciare, e il bilanciamento è dell'utente
  (`instrumentMix`).
- Niente compressione di bus, niente limiter sul master. Picco intorno a
  −6 dBFS, coerente fra i take.
- Stessa stanza, stessa posizione microfoni, stesse mani per tutta la serie: i
  file vengono incrociati fra loro e una differenza di timbro fra due take si
  sente al cambio.

### Come consegnarli

```
Assets/Loops/dance/
    bank.vploops
    dance_grooveA_110_straight_congas_t1.wav
    dance_grooveA_110_straight_congas_t2.wav
    ...
```

Nome file libero, ma tenere lo schema
`stile_ruolo_bpm_swing_stem_takeN.wav` rende il manifest leggibile e i bug
evidenti. Se i marker per quarto non li hai, lascia via la riga `beats`: con
loop tagliati esatti l'assunzione equispaziata è corretta.

## 5. Come si accende

```bash
cmake -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release -DVP_ENABLE_RECORDED_LOOPS=ON
```

e dal codice, a dispositivo chiuso:

```cpp
std::string error;
if (! engine.loadLoopBank ("Assets/Loops/dance/bank.vploops", error))
    /* il banco non è utilizzabile: `error` dice quale loop e perché */;
```

`setRecordedLoopsEnabled(false)` lo rispegne a runtime senza ricompilare.
`snapshot().loopPlaying`, `.loopPhaseMs` e `.loopHandovers` dicono cosa sta
succedendo.

Signalsmith Stretch è vendorizzato come submodule (MIT, insieme a
`signalsmith-linear`). Un albero senza submodule compila lo stesso e usa il
WSOLA interno, che **non è il suono di produzione**: è un pavimento.

```bash
git submodule update --init --recursive
```

## 6. I test

`./build-host/VPTests_artefacts/Release/VPTests --loops` — **51 test**, pochi
secondi, senza la suite neurale. Girano su tutti e due i backend: con i submodule
c'è Signalsmith, con `-DVP_USE_SIGNALSMITH=OFF` c'è il WSOLA interno, e devono
passare tutti e due.

| Criterio richiesto | Test |
|---|---|
| partenza sul primo quarto utile | `nothing sounds before the quarter the part comes in on` |
| nessun clic alla chiusura del loop | `closing the loop leaves no step in the waveform` |
| nessuna allocazione nel callback | `the audio callback allocates nothing, changes included` (+ la versione hybrid) |
| identico da 128 a 4096 campioni | tre test: numero di colpi, posizione, livello |
| cambi loop senza colpi doppi o mancanti | `a change of recording neither doubles nor drops a stroke` |
| errore di fase udibile < 5 ms | `every stroke lands within 5 ms of the quarter` — misurato a **2,35 ms** |
| continuità con piccole variazioni di BPM | `a slow tempo drift is followed` — **3,35 ms** su 3 BPM di deriva |
| fallback automatico durante variazioni live | `a tempo that is moving hands it straight back to single strokes` |
| STOP immediato | `STOP is immediate - not the next quarter, not the next bar` |
| cambio parte/swing senza STOP | `the part changed recording without a STOP`, più i test di swing |
| build macOS e iPadOS Release | CMake: il flag è ortogonale, il target iOS non cambia |

Numeri misurati con Signalsmith su materiale sintetico (un colpo per quarto):
latenza dello stretcher **5880 campioni**, identica a 128, 512 e 4096; errore di
fase a regime **−0,02 ms**.

## 7. Quello che questa versione non fa

- **Non c'è la libreria audio.** Vedi la lista al punto 4.
- Lo strato di accenti sopra la registrazione è a zero (vedi punto 2).
- Il banco si carica a dispositivo chiuso. Cambiare banco a metà brano
  richiederebbe un passaggio di consegne fra due `LoopBank`, che non serve
  finché lo stile non si cambia dal vivo.
- Metriche diverse da 4/4 si descrivono nel manifest ma l'orologio conta in 4/4:
  un loop il cui numero di quarti non è multiplo di 4 non si allinea alla
  battuta del brano.
- La misura vera è l'ascolto. Tutti i numeri qui sopra sono su materiale
  sintetico: dicono che il motore è corretto, non che il risultato è musicale.
