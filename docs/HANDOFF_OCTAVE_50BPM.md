# Handoff — item 1: 50 BPM letto come 100 (ottava metrica)

Documento di lavoro per l'item 1 di `docs/TODO.md`. Scritto perché il lavoro è
passato di mano più volte (Codex → ChatGPT → Claude) e ogni passaggio ha rischiato
di rifare la stessa indagine. **Leggi "Vie già chiuse" prima di scrivere codice.**

Ultimo aggiornamento: 2026-09-04.

---

## Il bug, in una riga

Groove da batteria a **50 BPM** (cassa 1/3, rullo 2/4, hi-hat a ottavi): il tracker
si aggancia agli **ottavi dell'hi-hat** e riporta ~100 BPM. Non è deriva di fase,
non è il PLL: è il **livello metrico** (l'ottava) scelto e poi difeso.

Riproduzione, pochi secondi, non serve la suite:

```bash
cd build-host && ninja VPProbe && ./VPProbe_artefacts/Release/VPProbe --trace --mixer plain 50 2>&1 | tail -3
```

Atteso oggi: `end=100.0` (sbagliato). Controllo che deve restare verde: stesso
comando con `100` → `end≈99.97`.

---

## Vie già chiuse (non ripercorrerle)

1. **Soglia sui downbeat "forti"** (prima versione, ChatGPT). Contava la distanza
   fra downbeat che superano `downThresh`: due bar da 8 battiti accettati invece di
   4 ⇒ ottava sbagliata. **Non scatta**: a 50 BPM la rete supera quella soglia
   troppo di rado perché il contatore chiuda due bar consecutivi.
2. **Probabilità continua di downbeat** (Claude). Istogramma a 8 posizioni sulla
   confidenza continua, con indice di fase preso dal **tempo trascorso / periodo**
   (non da un contatore di battiti: misurato che la griglia salta ogni tanto un
   ottavo, e un contatore si sfasa per sempre dopo il salto). Test: bin dominante
   con il bin opposto (4 via) debole ⇒ ottava sbagliata. **Non discrimina**: la rete
   attiva il downbeat *comparabilmente su vero battere 1 e vero battere 3* (la
   stessa ambiguità 1-vs-3 dell'item 2). A ottava sbagliata quei due punti stanno
   esattamente a 4 posizioni: identica firma del caso "ottava giusta".
3. **Ampiezza generale di attivazione** (beat, non downbeat). Ipotesi: gli ottavi
   off-beat (solo hi-hat) dovrebbero essere più deboli. **Falsa**: misurata alta
   (0.5–0.98) su quasi ogni ottavo — la rete tratta l'hi-hat come impulso saliente
   quanto cassa e rullo. È esattamente il motivo per cui si aggancia lì.
4. **Armonia** (`HarmonicChange` / `barFromHarmony`, come fa l'item 2 per l'1-vs-3).
   **Non applicabile**: il materiale del bug è batteria pura, non c'è contenuto
   armonico da cui estrarre un cambio.
5. **Energia di banda bassa per battito** (la pista descritta qui sotto, scritta e
   misurata). **Funziona sul caso target e rompe l'half-time**, e non per
   taratura: i due materiali producono lo stesso segnale. È la via che ha chiuso
   la questione — i numeri sono nella sezione "Esito". Il codice è ancora nel
   tree, spento (`kCadenceCorrectionEnabled`), come banco di misura.

Morale: con i **soli tre scalari** che il decoder riceve dalla rete
(`pBeat`, `pDownbeat`, `pNone`) il problema non è risolvibile — e con
l'informazione spettrale in più **non è risolvibile lo stesso**, perché il caso è
indecidibile in linea di principio, non per mancanza di segnale. Vedi "Esito".

---

## La pista che ha chiuso la questione: energia di banda bassa

`NeuralBeatTracker.cpp:193` estrae per ogni hop un vettore di **136 bande
logaritmiche** (30 Hz → 17 kHz, `LogSpectFeatures`) che alimenta la rete e poi
**viene buttato**. Cassa e rullo hanno corpo nelle bande basse; un hi-hat no.
Quindi: portare fino al decoder un solo scalare — l'energia delle bande basse di
quel frame — e usarlo per il test dell'ottava. Nessun nuovo filtro audio da
scrivere, nessun nuovo thread: il numero esiste già, va solo passato.

### Il confound da misurare *prima* di implementare

Un test ingenuo "l'energia bassa alterna forte/debole ⇒ ottava sbagliata" **non
basta**, perché alterna in entrambi i casi:

| | posizioni accettate | banda bassa |
|---|---|---|
| ottava **sbagliata** | cassa, hat, rullo, hat … | forte, **~niente**, medio, **~niente** |
| ottava **giusta** | cassa, rullo, cassa, rullo … | forte, medio, forte, medio |

In tutti e due i casi c'è alternanza a periodo 2. La differenza è la **profondità**:
nel caso sbagliato le posizioni deboli sono hi-hat, cioè quasi silenzio in banda
bassa; nel caso giusto sono rullo, che in banda bassa c'è eccome.

**Quindi la domanda empirica, e l'unica cosa da misurare in Fase 0:** quanto vale
il rapporto `debole/forte` a 50 BPM (caso rotto) e a 100 BPM (caso sano)? Se c'è un
divario netto e ripetibile, il correttore è una soglia in mezzo. Se il divario è
stretto, **fermarsi e scriverlo qui**: sarebbe l'ennesima taratura fragile.

---

## Piano

### Fase 0 — misura (prima di qualsiasi logica)
- [x] Portare l'energia di banda bassa da `NeuralBeatTracker` a `BeatDecoder`
      (serve comunque, ed è il minimo per poter misurare).
- [x] Accumularla nell'istogramma a 8 posizioni già scritto (indice di fase dal
      tempo, non dal conteggio).
- [x] Stampare il profilo a 50 e a 100 BPM e confrontare la profondità
      dell'alternanza. **Provare più di un intervallo di bande** (le prime ~8, ~12,
      ~24) perché al fondo la griglia FFT è grossolana e il numero di bande utili
      non è ovvio a tavolino.
- [x] **Esito scritto qui sotto**, numeri inclusi, anche se negativo.

### Fase 1 — correttore (fatto, e poi disattivato: vedi Fase 2)
- [x] Sostituito il test "downbeat appaiato" (via chiusa n.2) con il test di
      profondità sull'energia bassa. Uno solo, non due: il vecchio non scattava mai.
- [x] Soglia presa **in mezzo al divario misurato** (0.55 fra 0.43 e 0.85), con il
      commento che riporta la tabella.
- [x] Gate invariati: solo `useAnchor && lineFeed` (mixer/file, non microfono in
      stanza), evidenza minima accumulata, ottava proposta dentro il range.

### Fase 2 — verifica (economica, in quest'ordine)
- [x] `VPProbe --mixer plain 50` → **50.03**, il bug target è risolto.
- [x] `plain 76 / 100 / 118 / 132 / 140`, `syncopated`, `pad`, `sync+pad` → invariati.
- [x] `half-time 100` → **rotto** (99.92 → 60.13). A/B col correttore spento fatto:
      è il correttore, non altro. **Per questo è disattivato.**
- [ ] `VPTests --octave` — non serve più: il correttore non scatta, il
      comportamento è quello di prima (verificato sulla matrice probe qui sopra).
- [ ] Gate `VPTests` intera: **chiedere prima**, costa ~5 min.
- [ ] Ascolto su brano caricato/mixer (nessun orecchio umano finora).

### Fase 3 — docs
- [x] Aggiornare l'item 1 in `docs/TODO.md` con l'esito.
- [x] `.claude/skills/realtime-tempo/SKILL.md`, §2: nuova sottosezione "For one
      class of material it is not *partly* unsolvable - it is undecidable", con
      le tre vie tentate, i numeri e la tabella dei due groove identici. Nota:
      `.agents/skills/realtime-tempo/SKILL.md` è un mirror già divergente da
      prima (gli mancano intere sezioni di §2 e §5) e **non** è stato toccato.
- [x] Riaperto l'**item 15** (÷2/×2) nel TODO: la sua motivazione del 2026-09-03
      ("i casi ambigui vanno risolti nel tracker") presuppone che il tracker
      possa risolverli, e per questa classe di materiale è ora dimostrato che non
      può. La decisione va ripresa sapendo questo.

---

## Esito (misurato 2026-09-04) — **la pista si chiude, ma con un risultato utile**

### Fase 0: la profondità dell'alternanza, per numero di bande

`depth = min(pari, dispari) / max(pari, dispari)` sull'istogramma a 8 posizioni,
`--mixer plain`, a regime:

| bande basse | 50 BPM (lettura sbagliata) | 100 BPM (lettura giusta) | divario |
|---|---|---|---|
| 6  | 0.53 | 0.60 | 0.07 — inutile |
| 12 | 0.50 | 0.73 | 0.23 — stretto |
| **24** | **0.43** | **0.85** | **0.42 — netto** |

Perché 24: sei bande arrivano solo alla fondamentale della cassa, dove **anche il
rullo** sembra vuoto quanto un hi-hat e le due letture si avvicinano. Ventiquattro
arrivano al corpo del rullo: a livello giusto entrambe le metà sono piene (depth
→ 1), mentre l'hi-hat lì sotto continua ad avere poco.

### Fase 1: il correttore funziona sul caso target

Con `kLowBands = 24` e soglia 0.55:

```
plain 50  -> end=50.03   (era 100.02)   ← BUG RISOLTO
plain 100 -> end=99.96   invariato
plain 76 / 118 / 132 / 140  invariati
syncopated / pad / sync+pad 100  invariati
```

### Fase 2: e qui si rompe — `half-time`

```
half-time 100, correttore OFF -> end=99.92  (giusto)
half-time 100, correttore ON  -> end=60.13  span=34  jumps=23   ← ROTTO
```

**Non è una soglia da spostare.** Il renderer mette, in half-time, il rullo *solo
sul 3*: quindi a 100 BPM le posizioni 2 e 4 sono vuote in banda bassa —
**esattamente** come lo sono gli ottavi dell'hi-hat nel caso a 50 BPM. Le due
situazioni producono lo stesso istogramma perché **sono lo stesso suono**:

| | slot 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| straight a 50 (letto 100) | cassa | hat | rullo | hat | cassa | hat | rullo | hat |
| half-time a 100 (letto 100) | cassa | hat | rullo | hat | cassa | hat | rullo | hat |

Stessa spaziatura, stesso basso sotto gli stessi slot. **Nessuna misura sull'audio
può separarli, perché non c'è niente da separare.** Quale dei due livelli si chiami
"il tempo" è una **convenzione**, non un fatto acustico — e l'app una convenzione ce
l'ha già (il range riportabile `kMinBpm=50..kMaxBpm=215` e i limiti d'ottava in
`BeatTracker.cpp`).

### Conseguenza pratica (importante per l'utente)

Se il caso è davvero indecidibile dall'audio, l'unico modo di risolverlo è
**qualcosa che venga da fuori**:

- **TAP** (già presente): il musicista batte il tempo e dichiara lui il livello.
- Un **controllo ÷2 / ×2**: erano stati **rimossi** nell'item 15 del TODO perché
  sembravano ridondanti con l'ottava automatica. Questo risultato dice che non lo
  erano: esiste una classe di materiale dove l'automatismo *non può* decidere, e lì
  un tasto è l'unica risposta corretta. **Vale la pena riaprire l'item 15 alla luce
  di questo.**
- Un limite inferiore del range: `kMinBpm = 50` significa che 50 BPM è già il
  fondo della scala rappresentabile, e il caso dell'utente sta esattamente lì.

### Attenzione: il test `VPTests` su questo caso passa, e non vuol dire che funzioni

In `Tests/TestAiBeat.cpp` c'è **"a 50 BPM bar makes its 100 BPM hi-hat pulse count
as eighths"** (scritto dalla sessione precedente). Passa. Ma guarda il fixture
prima di fidartene: alimenta il decoder con attivazioni sintetiche in cui

- gli ottavi off-beat stanno a **0.62** contro **0.95** dei quarti — un dislivello
  netto e pulito;
- la curva di downbeat vale **0.90 solo sul vero uno** e 0.02 ovunque altro — nessuna
  ambiguità 1-vs-3.

Sono **esattamente le due proprietà che ho misurato false sulla rete vera**: lì
l'attivazione sta fra 0.5 e 0.98 su *ogni* ottavo, e la massa di downbeat si divide
fra il vero uno e il vero tre. Il fixture ha progettato via proprio l'ambiguità che
rende il caso difficile, quindi il test verifica il contatore, non il problema.

Chi legge quel PASS come "il caso a 50 BPM è risolto" sbaglia: il probe sullo stesso
albero dice `plain 50 -> end=100.05`. **Il test verde e il bug aperto convivono, ed è
corretto che convivano** — semplicemente misurano cose diverse.

(Nota di percorso: sostituendo il contatore con il test sull'energia bassa avevo
rotto quel test senza accorgermene; il contatore è stato ripristinato e i due
meccanismi ora convivono — vedi sotto.)

### Stato del codice lasciato nel tree

Ci sono **due** meccanismi, e fanno cose diverse:

1. **`BeatDecoder::observeDownbeatCadence()`** — il contatore di spaziatura fra
   downbeat sopra soglia, della sessione precedente. **Attivo.** È quello che fa
   passare il test descritto qui sopra. Sulla rete vera non è mai stato misurato
   scattare: né sul caso a 50 BPM (per questo l'item è ancora aperto) né altrove
   sulla matrice di probe. È l'unico dei due che può produrre `metricalOctaveHint`.
2. **`BeatDecoder::observeMetricalCadence()`** — l'istogramma sull'energia di banda
   bassa, questa sessione. **Spento** da `kCadenceCorrectionEnabled = false`:
   accumula e misura, ma non tocca il hint. È il banco che ha prodotto i numeri di
   questo documento. Il commento sopra la costante li riporta.

Il resto:

- `LogSpectFeatures::lowBandEnergy` + `kLowBands = 24` — nuovo, piccolo, alimenta (2).
- `NeuralBeatTracker.cpp` passa quel valore a `BeatDecoder::observe` (4° parametro,
  default 0 per test e probe che alimentano le attivazioni a mano — ecco perché il
  test sintetico non vede banda bassa e (2) resterebbe muto lì comunque).
- `metricalOctaveHint` in `BeatHypothesis`, `BeatTracker::updateAutoOctave` che lo
  consuma, `kOctaveTooSlow` 76→49: sessione precedente, lasciati com'erano.

**Se accendi (2), spegni (1)** — altrimenti due meccanismi scrivono lo stesso hint
con criteri diversi. E prima di accenderlo rileggi perché è spento.

Per rimisurare tutto: rimetti `kCadenceCorrectionEnabled = true` e lancia i comandi
della sezione "Regole di ingaggio".

---

## Regole di ingaggio (valgono per chiunque riprenda)

- **Non lanciare `VPTests` intera per iterare.** Usa `VPProbe` (secondi) o
  `VPTests --octave` (~3-4 min). La suite intera solo come gate finale, chiedendo.
- Non "sistemare" bloccando l'ottava a mano o allargando il prior dell'HMM: già
  provato e annotato nel codice (`BeatDecoder`, commento su `setLevelAnchor`) —
  sistema 72 BPM, lascia 60 e 190 dove sono, e rompe "gli ottavi forti non sono il
  beat".
- Il correttore non deve toccare il path microfono/stanza (`speakerFollow`): lì la
  curva è troppo sporca, e non è il path del bug.
- Se una via si chiude, **scrivila qui sopra** con il numero che l'ha chiusa.
