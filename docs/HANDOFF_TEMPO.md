# Ripresa del lavoro sul tempo

Richiesta: implementare progressivamente il piano approvato, con test mirati e
commit locali separati. **Ultima istruzione utente: proseguire SENZA committare**;
questa prevale sul piano iniziale. Non eseguire la suite completa. Nessuna modifica al
submodule JUCE, già sporco all'inizio. Nessuna promessa di perfezione su audio
ambiguo: mantenere il tempo acquisito, oppure attendere/TAP all'avvio.

## Stato

1. COMPLETATO: sicurezza del rientro (annullamento, reset, prove fresche).
2. COMPLETATO: contatori indipendenti dal buffer.
3. COMPLETATO (clock sintetico): recupero dello scarto con fiducia alta.
4. COMPLETATO (integrazione con date accordi note): percorso armonico diretto.
5. INCOMPLETO: il percorso armonico su audio sintetico ritmico ora aggancia e
   rende sul tempo, ma non entro due battute; il caso con pad fallisce ancora.
   La registrazione live dell'utente e' stata provata: il mantenimento a livello
   normale e' buono, ma a -12 dB l'avvio resta per circa 10 s sull'ottava alta.

## Regole per riprendere

Leggere `.claude/skills/realtime-tempo/SKILL.md`, questo file e `git status`.
Continuare dal primo step incompleto. Salvare qui comandi, risultati, limiti e
prossima azione dopo ogni step; non creare commit finché l'utente non lo richiede.
I precedenti 8 ms / mezzo beat erano misure del solo
clock con fase esatta e fiducia simulata, non dell'app sul brano live.

## Verifica prevista

Probe brevi a 52/100/168 BPM, entrambe le direzioni; jitter, ritorno di evidenza
povera, reset, pubblicazioni ripetute e buffer 64/256/1024. Separare ritardo di
riconoscimento, convergenza del clock e attacchi audio. Per l'armonia verificare
inizializzazione a sample rate diverso, scadenza, accordi radi/sostenuti,
ingresso effettivo e ritorno della batteria. Il percorso acustico armonico
resta disabilitato finché non supera una verifica con stanza e ritorno proprio.

## Step 1 — verifica

`c++ -std=c++17 -O2 -ISource scripts/probe_recovery.cpp Source/Tracking/TempoFollower.cpp -o /tmp/vp-recovery && /tmp/vp-recovery`

6/6 PASS, 52/100/168 BPM, entrambi i segni. Due seriali distinti confermano;
copie della stessa pubblicazione non contano. Peggioramento, snap, reset,
forceTempo e transizione confermata cancellano il recupero. Da conferma a 8 ms:
0.571 / 0.299 / 0.176 s; errore successivo <8 ms per oltre due beat. Questo è
il clock, non il modello reale. Il vecchio `probe_steer --reentry` viene rifiutato
con istruzioni perché non forniva beat freschi. Prossimo: step 2.

## Step 2 — verifica

`cmake --build build-host --target VPTests -j4`
`./build-host/VPTests_artefacts/Release/VPTests --state-timing`

3/3 PASS: soglia di quattro secondi attraversata a 4.001333 / 4.005333 /
4.010667 s con buffer 64/256/1024, entro un callback. Anche il filtro della
fiducia ora usa il tempo reale, preservando la risposta precedente a 256/48k.
Step 1 salvato nel commit cdcb33c. Prossimo: step 3.

## Step 3 — verifica

Stesso comando standalone dello step 1: 12 casi di recupero + 3 controlli.
Fiducia alta: conferma dopo 1.157 / 0.603 / 0.357 s, convergenza aggiuntiva
0.576 / 0.299 / 0.176 s a 52/100/168; entrambi i segni, errore stabile <8 ms.
Rumore di fase, outlier isolato e rampa: nessuna attivazione, errore uguale al
controllo senza conferme. Due beat devono concordare entro 0.015 beat dopo la
compensazione, entrambi oltre max(0.04 beat, 20 ms). Cooldown di 2.5 beat.
La verifica dello swing del decoder e dell'audio completo resta allo step 5.
Prossimo: step 4. Step 2 commit d52f099.

## Step 4 — verifica e limite importante

`cmake --build build-host --target VPTests -j4`
`./build-host/VPTests_artefacts/Release/VPTests --harmonic-entry`

4/4 PASS: a 44.1/48 kHz il tracker entra senza worker neurale, grazie a otto
cambi di accordo datati, 100.000 BPM. PercussionEngine produce sei attacchi
misurati: errore massimo 2.59/0.23 ms. Microfono escluso, ritorno del BPM
neurale prioritario, scadenza dopo due battute (max 12 s), reset completo.
Il selettore armonico sceglie ora il massimo locale anziché il fianco della
curva: il vecchio confronto dei pareggi portava 100 esatti a 98.039.

LIMITI: questa prova bypassa il rilevatore di accordi, non il tracker/render.
Con un accordo per battuta servono otto accordi: ingresso a 18.79 s, NON entro
due battute. L'armonia rada da sola non soddisfa quell'obiettivo. Nessun nuovo
modello di pulsazione non percussiva è stato aggiunto. Step 5 deve misurare la
catena con il rilevatore e distinguere le registrazioni reali dai sintetici.

## Checkpoint finale — 08/09

Commit degli step: `cdcb33c`, `d52f099`, `cdf807f`, `9fcaf02`.
Ultimo controllo aggiunge annullamento del recupero anche su cambio d'ottava e
ricostruzione della griglia neurale; il probe verifica anche il cambio d'ottava.

Comandi mirati eseguiti:

- `c++ -std=c++17 -O2 -ISource scripts/probe_recovery.cpp Source/Tracking/TempoFollower.cpp -o /tmp/vp-recovery && /tmp/vp-recovery`: 15 casi PASS.
- `cmake --build build-host --target VPTests -j4`: compilazione riuscita.
- `./build-host/VPTests_artefacts/Release/VPTests --state-timing`: 3 PASS.
- `./build-host/VPTests_artefacts/Release/VPTests --harmonic-entry`: 4 PASS.
- `./build-host/VPTests_artefacts/Release/VPTests --tempo-slow`: 10 PASS, inclusi swing e ritorno dopo gap.
- `./build-host/VPTests_artefacts/Release/VPTests --octave 100-file`: 4 PASS. BeatNet reale su KIT SINTETICO: 99.97 BPM, ottava zero, FOLLOWING per 41.2 s, fase media +1.86 -> -1.23 ms, 137 colpi, zero gap, worker sincronizzato.

Le medie di fase del test BeatNet non sono un massimo degli attacchi audio.
Gli attacchi audio sono stati misurati separatamente nel test armonico con date
note. La registrazione live dell'utente e' stata provata nel checkpoint seguente;
non e' stata fatta una nuova build su iPad. Non e' stata eseguita la suite
completa.

## Prossima azione concreta

Il nuovo `--harmonic-audio` alimenta HarmonicChange con i soli stems musicali
di `probe_song_render.h` (basso e melodia, con/senza pad sostenuto), poi usa
BeatTracker e PercussionEngine nello stesso percorso. 36 s sintetici a 100 BPM,
48 kHz, buffer 256, seed 42; nessuna data di accordo iniettata, nessun worker
neurale. Include gli eventi iniziali del detector come in produzione. Esclusi
condizionamento del bus, stanza e ritorno delle percussioni. Il caso col pad
NON è un accordo isolato senza pulsazione: conserva basso e melodia.

Comandi eseguiti (nessuna suite completa, nessun commit):

- `cmake --build build-host --target VPTests -j4`: PASS.
- `./build-host/VPTests_artefacts/Release/VPTests --harmonic-audio`: exit 1, due FAIL, circa 0.8 s host.
- `./build-host/VPTests_artefacts/Release/VPTests --harmonic-entry`: quattro PASS, controllo con date note invariato.

Seconda iterazione, senza commit: `HarmonicChange` ora usa in produzione lo
stesso warm-up di quattro secondi che il vecchio VPSing applicava solo nel
probe. Gli eventi instabili di inizializzazione non contaminano più
HarmonicTempo. La selezione della fonte non richiede più anche il lento
`tonalShare > 0.55`: una fase armonica valida è già fondata su otto cambi
freschi e coerenti; qualsiasi BPM neurale resta immediatamente prioritario.
Il gate di tonalità rimane sul percorso separato che sposta la battuta.

Risultati del gate, che richiede ingresso entro 4.8 s e attacchi entro 25 ms
nelle ultime due battute:

- Senza pad: 13 cambi; fase e fonte valide da 22.096 s, ingresso a 23.371 s,
  99.917 BPM, coerenza 0.997. Otto impulsi del clock e otto attacchi; errori
  massimi 22.27 ms e 18.17 ms. Funziona, ma fallisce il limite di 4.8 s.
- Con pad: 34 cambi; fase mai valida, coerenza finale 0.355. Nessun ingresso,
  nessun attacco. Il solo numero di eventi non prova quali siano falsi.
- Batteria sola: 7 cambi, quota tonale max 0.252, nessuna fase/BPM: astensione PASS.
- Accordo continuo: quota tonale max 0.814 ma zero cambi e zero BPM: astensione PASS.
- Errori fase/clock/audio stampati a -1 quando non misurabili: NON zero errore.

Terza iterazione, senza commit: il gate ora esegue anche il vero worker BeatNet
sugli stessi stems senza batteria, sincronizzato a ogni hop e senza backlog.
Dodici secondi per caso, seed 143:

- 52 senza pad: ingresso 5.184 s (entro 9.231), ma 103.927 BPM, fit
  0.003/0.833: ottava errata con evidenza internamente coerente.
- 52 con pad: ingresso 10.037 s e 147.282 BPM: FAIL.
- 100 senza pad: ingresso 2.347 s (entro 4.8), 101.940 BPM: PASS.
- 100 con pad: ingresso 5.099 s, 60.685 BPM: FAIL.
- 168 senza pad: ingresso 2.309 s, ma 91.585 BPM: FAIL.
- 168 con pad: ingresso 3.568 s, 83.565 BPM: FAIL.

Quindi una nuova sorgente di onset generica non è una correzione sicura: sui
due estremi il modello vede una griglia buona precisamente all'ottava sbagliata.
La prova deliberata con soli due cambi armonici conferma il rischio: il pad
raggiunge temporaneamente 200 BPM e la batteria sola coerenza 1.0. Le protezioni
sono state ripristinate (warm-up 4 s, otto cambi, tonalShare lento); nessuna di
quelle soglie sperimentali è rimasta nel codice.

La referenza armonica matura riconosce 168 come 169.492 BPM dopo 16.427 s, ma a
52 non produce una fase valida neppure nella finestra estesa. Non può dunque
essere usata come correzione generale, e applicarla a parte già entrata
violerebbe inoltre la regola di non cambiare ottava mentre suona.

La strumentazione e i controlli negativi sono pronti, ma lo step NON è
completato. Gli otto cambi richiesti restano incompatibili con due battute
quando c'è un solo accordo per battuta. Se il test reale senza percussioni
conferma l'errore, serve un modello di beat/downbeat addestrato o calibrato anche
su accompagnamenti tonali; non un altro peak picker, che non può distinguere
52/104 e 84/168 su questi segnali radi. Fino ad allora attendere/TAP o i
controlli ÷2/×2 restano la risposta deterministica ai casi acusticamente
ambigui. L'obiettivo entro due battute resta aperto per l'armonia rada: non
abbassare il numero di cambi alla cieca, né attivare il percorso acustico non
verificato.

## Checkpoint registrazione live — 08/09/2026

File esterno al repository:
`/Users/nicolamarogna/Desktop/3 INFINITO.mp3` (296.908 s, stereo, 44.1 kHz,
128 kbit/s). `VPLive` in questa build non apre MP3; la conversione e gli
estratti WAV sono stati creati soltanto in `/tmp` con `mpg123`. L'originale non
e' stato modificato. Build e prove mirate, nessuna suite completa e nessun
commit:

```bash
cmake --build build-host --target VPLive -j4
/opt/homebrew/bin/mpg123 -q -w /tmp/vp-infinito.wav \
  '/Users/nicolamarogna/Desktop/3 INFINITO.mp3'
./build-host/VPLive_artefacts/Release/VPLive \
  --mix /tmp/vp-infinito.wav --auto --trace
./build-host/VPLive_artefacts/Release/VPLive \
  --mix /tmp/vp-infinito.wav --bpm 91 --trace
```

Risultato sul file intero: il brano parte intorno a 72 BPM, raggiunge circa
91 BPM verso 14-18 s, resta poi stabile e rallenta nel finale verso 84-82 BPM.
Il passaggio `--bpm 91` sull'intero file misura 90.54 BPM medi e 23.4 ms di
deriva media, ma il massimo di 571.8 ms NON e' attribuibile al follower: il
riferimento costante e' incompatibile con intro e finale variabili.

La modalita' `--auto` trova 3274 attacchi, 11.0/s. A 91 BPM i sedicesimi sono
solo 6.07/s: la referenza comprende note e rumore fuori griglia. I suoi 46.7 ms
di dispersione e il 29% entro 20 ms non sono quindi una misura valida della
fase o degli attacchi audio. Non usare questi numeri per regolare il PLL. Per
misurare la fase assoluta di questo brano serve una beat-grid annotata oppure
stems/click isolati; `--bpm` misura soltanto la deriva dopo avere rimosso
l'offset mediano.

Estratto centrale 30-250 s, dove 91 BPM e' un riferimento ragionevole:

```bash
/opt/homebrew/bin/mpg123 -q -k 1148 -n 8422 \
  -w /tmp/vp-infinito-center.wav \
  '/Users/nicolamarogna/Desktop/3 INFINITO.mp3'
./build-host/VPLive_artefacts/Release/VPLive \
  --mix /tmp/vp-infinito-center.wav --bpm 91 --trace
```

Risultato: 90.99 BPM medi (scarto 0.01%), deriva media 8.5 ms, massimo 89.7 ms.
Non appare deriva progressiva. Da avvio freddo il clock pubblica 95.57 BPM a
2 s, 95.43 a 4 s, 92.31 a 6 s e 91.45 a 8 s; entra a 2 s e dichiara `FISSO`
intorno a 14 s. E' utilizzabile vicino al limite di due battute a 91 BPM
(5.27 s), ma la certezza piena e' ancora troppo lenta.

### Regressione dipendente dal livello — riproducibile

Stesso estratto 30-120 s, senza cambiare il contenuto musicale:

```bash
/opt/homebrew/bin/mpg123 -q -f 8192 -k 1148 -n 3445 \
  -w /tmp/vp-infinito-quiet.wav \
  '/Users/nicolamarogna/Desktop/3 INFINITO.mp3'
/opt/homebrew/bin/mpg123 -q -f 65536 -k 1148 -n 3445 \
  -w /tmp/vp-infinito-hot.wav \
  '/Users/nicolamarogna/Desktop/3 INFINITO.mp3'
./build-host/VPLive_artefacts/Release/VPLive \
  --mix /tmp/vp-infinito-quiet.wav --bpm 91 --trace
./build-host/VPLive_artefacts/Release/VPLive \
  --mix /tmp/vp-infinito-hot.wav --bpm 91
```

- Livello basso (`-f 8192`, ampiezza 0.25, circa -12 dB): il decoder parte a
  212.61 BPM; clock 202.27 a 2 s, circa 208-211 fino a 10 s, poi snap a 91.19
  a 12 s. `FISSO` soltanto verso 20 s. Media della finestra 94.94 BPM, deriva
  media 15.7 ms, massimo 257.7 ms. **Fallisce chiaramente le due battute.**
- Livello alto (`-f 65536`, con clipping possibile): 91.06 BPM medi, deriva
  media 5.3 ms, massimo 19.0 ms. Nessuna perdita di tempo osservata.

Il difetto confermato da questa registrazione non e' quindi "volume alto fa
perdere il tempo", ma **il livello basso consente una falsa ottava alta durante
l'acquisizione iniziale**. Una volta stabilizzato a 91 BPM il mantenimento e'
buono. Non e' ancora noto se la causa primaria sia il preprocessing del tensore
BeatNet, la soglia delle attivazioni o l'arbitraggio iniziale del decoder.

## Lavoro ancora necessario, in ordine

1. **Rendere BeatNet indipendente dal livello prima di cambiare il PLL.**
   Strumentare solo il probe/offline path e confrontare, nei primi 12 s, RMS e
   picco del segnale, range del tensore inviato a ONNX e attivazioni beat per
   originale, -6, -12 e -18 dB. Verificare che il frontend applichi esattamente
   la normalizzazione usata in addestramento. Non inserire AGC nel bus audio;
   un eventuale condizionamento deve riguardare esclusivamente la copia del
   segnale destinata al modello.
2. **Correggere l'arbitraggio iniziale 91/182-212.** Nel caso debole il pettine
   vede gia' circa 91 BPM a 8-10 s mentre la rete pubblica circa 208. Prima di
   pubblicare o mantenere un'ottava estrema, usare evidenza fresca e concordante
   tra le sorgenti gia' esistenti. Non aggiungere un altro onset picker e non
   riavviare il clock. Conservare la precedenza dei cambi di tempo confermati.
3. **Aggiungere una regressione mirata del livello.** Sweep almeno -18/-12/-6/
   0 dB e caso clippato, su 52/91-100/168 BPM; misurare separatamente tempo alla
   prima ottava corretta, ingresso delle percussioni, `FISSO`, deriva e recupero.
   Obiettivo sul caso riconoscibile: ottava e ingresso entro due battute, poi
   stabilita' per almeno due beat. Eseguire solo il nuovo filtro di test.
4. **Ottenere una verita' di fase per la registrazione.** Annotare manualmente
   una breve beat-grid nelle sezioni 30-120, 120-210 e 210-250 s, oppure ottenere
   uno stem/click affidabile. Solo allora misurare clock e attacchi audio assoluti
   ed esaminare i picchi isolati da 85-167 ms osservati nei tagli a freddo.
5. **Completare il caso senza batteria.** Provare stems o una registrazione in
   cui la pulsazione tonale sia isolabile. Il test armonico sintetico con pad e'
   ancora fallito e la fonte armonica richiede troppo tempo a 52 BPM. Se BeatNet
   non generalizza, valutare un modello beat/downbeat adatto a materiale tonale;
   non indebolire alla cieca warm-up, otto cambi o `tonalShare`.
6. **Verificare infine su iPad**, includendo variazione del gain hardware,
   attivazione/disattivazione delle percussioni e ritorno acustico. Il probe host
   non simula microfono, stanza, latenza I/O o feedback degli strumenti.

Lo step 5 resta aperto. Criterio di chiusura: su registrazioni con pulsazione
riconoscibile, ottava corretta e ingresso entro due battute; dopo un rientro,
almeno due beat stabili. Riportare separatamente sintetico, file reale annotato
e prova iPad. Dopo ogni correzione aggiornare questo file con comando, misura e
limite; non creare commit finche' l'utente non lo richiede.

## Punto 1 — livello e frontend BeatNet (08/09/2026)

Strumentato **solo il percorso probe/offline**: `scripts/probe_activations.cpp`
prende ora `--wav file.wav --sweep [--secs 12] [--gains 0,-6,-12,-18]` e stampa,
per ogni guadagno, RMS e picco del segnale, l'intervallo del tensore inviato a
ONNX (metà magnitudine e metà differenza separate) e le attivazioni del modello.
Il guadagno è applicato **soltanto alla copia destinata al modello**; nessuna
AGC nel bus audio, nessuna modifica al codice di produzione. Nessun commit.

```bash
cmake --build build-host --target VPActivations -j4
./build-host/VPActivations_artefacts/Release/VPActivations \
  --wav /tmp/vp-infinito-30-120.wav --sweep --secs 12 --gains 0,-6,-12,-18
```

Estratto 30-120 s della registrazione dell'utente, primi 12 s, 596 fotogrammi
per riga, ONNX reale:

| gain | rmsDb | peakDb | magMax | magMean | diffMean | pBeat medio | pBeat>0.5 | pDown medio | pDown>0.5 |
|---|---|---|---|---|---|---|---|---|---|
| 0 | -19.5 | -4.0 | 2.0804 | 0.2395 | 0.0301 | 0.0681 | 33 | 0.0352 | 17 |
| -6 | -25.5 | -10.0 | 1.7839 | 0.1595 | 0.0216 | 0.0785 | 23 | 0.0840 | 30 |
| -12 | -31.5 | -16.0 | 1.4910 | 0.1020 | 0.0148 | 0.0764 | 15 | 0.1441 | 57 |
| -18 | -37.5 | -22.0 | 1.2047 | 0.0624 | 0.0096 | 0.0690 | 3 | 0.1685 | 67 |

Nessun campione clippato in nessuna riga. La media di `pBeat` è quasi costante e
quindi **non è una misura utile**; quello che cambia è la forma: i fotogrammi di
battito confidenti scendono da 33 a 3, mentre le attivazioni di downbeat sopra
0.5 salgono da 17 a 67. A -12 dB il modello riceve una metà-differenza (la
feature di attacco) ridotta a circa la metà, e restituisce un'evidenza
qualitativamente diversa, non semplicemente più debole.

### Il frontend applica la normalizzazione dell'addestramento

BeatNet usa `LOG_SPECT` di madmom a 22050 Hz, finestra 1411, hop 441, 24
bande/ottava 30-17000 Hz, filtri a somma 1, `log10(1 + x)` — gli stessi valori
già in `BeatModelConfig.h`. madmom scala la finestra per `1/iinfo(int16).max`
sui file interi, quindi il modello vede campioni in ±1 con FFT **non**
normalizzata.

Verifica numerica con un seno a 1 kHz a fondo scala, 22050 Hz (WAV generato
nello scratchpad, `--sweep --secs 3`):

- riferimento madmom analitico: `|X|` di picco `352.1` (atteso `A·N/4 = 352.75`)
- il nostro frontend: `magMax = 2.2666`, cioè `log10(1 + 183.6)`

`183.6 / 352.1 = 0.52` è esattamente il peso che il filtro triangolare a somma 1
dà al bin di picco. Scala, finestra e filterbank coincidono. Un errore di
normalizzazione plausibile darebbe ordini di grandezza diversi: FFT divisa per N
darebbe circa `0.05`, campioni in scala int16 darebbero circa `6.8`.

**Limite:** madmom non è installato su questa macchina (richiede numpy < 2), e
non è stato quindi fatto un confronto fotogramma per fotogramma con
l'implementazione originale. Il controllo sopra verifica la scala e la catena,
non ogni dettaglio del filterbank.

Lo stesso seno scende da `magMax` 2.2666 a 1.3826 a -18 dB: la dipendenza dal
livello è **intrinseca a `log10(1 + x)`**, non un difetto della nostra porta.
BeatNet ha la stessa proprietà; è stato addestrato su musica a livello naturale,
quindi un ingresso a -12 dB è fuori dalla distribuzione di addestramento.

### Conseguenza per i punti successivi

Il punto 1 è **misurato e chiuso come diagnosi**: non c'è una normalizzazione da
correggere nel frontend. Restano due vie, in quest'ordine:

1. Il punto 2 (arbitraggio iniziale) resta necessario e indipendente dal livello:
   con evidenza povera non si deve pubblicare né mantenere un'ottava estrema.
2. Solo dopo, se serve, un condizionamento lento della **sola copia del segnale
   destinata al modello**, che porti l'RMS verso il livello dell'addestramento.
   Non è stato implementato qui: con un file è tautologico (riporta esattamente
   la riga a 0 dB) e va misurato con una costante di tempo reale e la
   regressione del punto 3, non con un guadagno costante.

Prossima azione: punto 2, l'arbitraggio iniziale 91/182-212.
