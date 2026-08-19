# Stato dell’app — Virtual Percussionist

Documento operativo. Aggiornarlo ogni volta che cambia cosa si può installare, cosa locka, e cosa resta da provare sul device.

**Data:** 19 agosto 2026 (sera)  
**Target:** iPadOS 16+, iPad Air M1  
**Versione albero:** 0.1.0

## Pronto per l’iPad

Sì: apri il progetto Xcode, seleziona l’iPad, Run. Nel binario c’è **BeatNet** (pesi GTZAN) + ONNX Runtime iOS (CPU + CoreML).

Dopo questo cambio **riconfigura** iOS (`./scripts/configure-ios.sh`) e reinstalla: il lock SPEAKER/Spotify è cambiato nel C++.

## Ultima verifica automatica (19 agosto 2026, sera)

| Check | Esito |
|---|---|
| Host `VPTests` | **71 passed, 0 failed** — kit/CLICK/quiet SPEAKER 120.0 stabile; aggancio di fase a 78/100/138 BPM entro 1 ms; battuta che non riparte; tempo fisso che si ferma |
| AI | Snapshot `aiOnnx=1`: motore **ONNX BeatNet**, non lo stub |
| Modello | BeatNet BDA GTZAN, LSTM streaming, `Assets/Models/beatnet.onnx` ~1.6 MB |
| Flags | `VP_USE_ONNX=1`, `VP_ORT_COREML=1`, `VP_HAS_BEAT_MODEL=1` |

## Bug Spotify / BPM a 0 (18 agosto)

L’AI **c’era** e il modo **IPAD/SPEAKER è quello giusto** (il microfono sente il mix in stanza; iOS non dà il PCM di Spotify).

Non lockava perché:

1. `heldBpm` veniva azzerato se il picco mic era sotto 0.0055 — tipico iPad speaker → stesso mic
2. Il lock richiedeva `heldBpm > 50` **dopo** quell’azzeramento (chicken-and-egg)
3. Makeup analysis non partiva sotto 0.0055
4. BeatDecoder voleva `pBeat > 0.32`; BeatNet su mix reale spesso sta sotto, quindi `valid=0` e BPM `--`

Ora: BPM neurale visibile anche in ascolto; makeup da ~0.0005; lock SPEAKER più permissivo; gate BeatNet 0.40 sull’attivazione combinata beat/downbeat; confidenza stabile tra un beat e l’altro.

UI: riga **AI ONNX | IPAD | nn | pBeat | valid**. Pannello DEBUG (in alto a destra) con onnx / source / mic vs analysis peak.

## START bloccato su ATTENDO BATTUTA + BPM che salta (18 agosto, pomeriggio)

Due bug dopo il lock SPEAKER più permissivo:

1. `pBeat`/`pDownbeat` sono probabilità **per frame**, non eventi. Ogni callback sembrava un beat/downbeat → `snapDownbeat` riportava la battuta a 1 e il clock non chiudeva mai la battuta → START restava su **ATTENDO BATTUTA**.
2. Con FOLLOWING, il target BPM inseguiva ogni stima neurale (e `lostSync` veniva azzerato ogni blocco).

Ora: solo i picchi del decoder contano; niente snap/griglia mentre si aspetta il 1; dopo ~2 bar o 3 s si parte sul prossimo quarto; BPM locked non rincorre salti > ~4.5% finché il disaccordo non è sostenuto.

## BPM Spotify fisso ma oscillante (18 agosto, pomeriggio)

`AI ONNX` confermava solo il caricamento della rete, non che il decoder fosse equivalente a BeatNet. Il problema era reale e riproducibile con il file ufficiale BeatNet `808kick120bpm.mp3`:

- le feature locali usavano 16 bande/ottava; BeatNet usa 24 bande/ottava sulla griglia madmom, con filtri duplicati rimossi fino a 136 bande;
- `diff_ratio=0.5` era interpretato erroneamente come `current - 0.5 * previous`; madmom calcola `current - previous`;
- il decoder guardava solo `pBeat`, ma il decoder ufficiale usa `max(pBeat, pDownbeat)` perché il downbeat è anche un beat;
- una curva di attivazione larga veniva contata più volte;
- la mediana di 8 IOI adiacenti poteva seguire sincopi/ottavi invece del periodo fondamentale.

Correzioni: filterbank allineata a madmom, differenza corretta, massimi locali causali, gate combinato 0.40 e consenso su 32 IOI includendo somme di 1–4 intervalli.

Evidenza diagnostica sul file ufficiale a 120 BPM:

- prima: `pBeat max 0.114`, nessun tempo valido;
- dopo feature corrette: `pBeat max 0.681`, `pDownbeat max 0.970`, **120.0 BPM stabile**;
- test host finale: **30 passed, 0 failed**.

Nota onesta: non è ancora incluso il particle filter Monte Carlo completo del progetto Python BeatNet. La rete ONNX è quella ufficiale; il decoder C++ è causale e stabilizzato per mobile.

## BPM al doppio e instabile (19 agosto)

Il sintomo — BPM riconosciuto al doppio, e comunque mai fermo — era reale e riproducibile. Misurato end to end su 120 brani sintetici (60–176 BPM, quattro stili, percorso SPEAKER con simulazione cassa→stanza→microfono):

| | prima | dopo |
|---|---|---|
| Ottava sbagliata | 41/120 | **25–26/120** |
| BPM instabile (span > 4 BPM nello stesso brano) | 39/120 | **24–29/120** |
| Cambi di livello metrico (52 tracce di attivazione) | 1590 | **15** |
| Test host | 36 | **42** (6 nuovi, ognuno verificato che fallisce sul codice vecchio) |

I due valori «dopo» sono due esecuzioni della **stessa** build: la sonda pilota il motore più veloce del tempo reale e il worker neurale gira su un altro thread, quindi lo scheduling sposta qualche caso da un run all'altro. Vanno letti come ordine di grandezza, non come cifre esatte. Le due colonne sono un run ciascuna.

Cause, tutte verificate con misure e non per ipotesi:

1. **L'autocorrelazione non sa scegliere l'ottava.** Sulle attivazioni BeatNet reali la correlazione a 1, 2, 3 e 4 battiti sta fra 0.78 e 0.97 per tutte: un treno di battiti correla con sé stesso a qualsiasi multiplo. A decidere il livello restava quindi il *prior* gaussiano su 120 BPM, che regala al doppio un vantaggio del 46 % a 60 BPM — cioè esattamente «lento → doppio, veloce → metà».
2. **Il livello si decide sull'ampiezza, non sulla correlazione.** Ripiegando l'attivazione sul periodo candidato, l'altezza a mezzo periodo dal picco vale ~0.13 sul battito vero e ~0.80 sul doppio. È una decisione, non un pareggio.
3. **Il livello ballava fra un refresh e l'altro.** Ora cambia solo se un rivale vince di un margine e lo mantiene per ~2 s.
4. **Il decoder non seguiva la correzione.** Il re-anchor era condizionato alla *clarity* del comb — che è bassa proprio quando la griglia sbagliata è il rivale — e vietato del tutto finché la griglia «stava bene», cosa sempre vera per il doppio del tempo giusto.
5. **Il livello di analisi era parte del problema.** Le feature sono `log10(mag + 1)`, quindi il guadagno conta. Il target era 0.12; l'ottimo misurato è **0.20**. E il guadagno inseguiva il picco istantaneamente, spostando il punto di lavoro della rete a ogni colpo di batteria.

## Tempo che oscilla su Spotify e battuta che riparte (19 agosto, sera)

I tre sintomi riportati provando con Spotify — **riconoscimento lento**, **BPM
che oscilla anche su un brano registrato in studio**, e **la battuta che fa
«uno, due» e torna all'uno** — erano tutti reali e tutti riproducibili. Col
click funzionavano perché un click non ha niente di quello che li causa.

### Come sono stati misurati

Serviva materiale che si comportasse come un disco, non come un segnale di
prova. `scripts/probe_song.cpp` (target `VPProbe`) genera trenta brani a tempo
**perfettamente fisso** — cinque arrangiamenti × sei tempi — con basso tenuto,
pad senza attacco, hi-hat sugli ottavi, sincopi, fill ogni otto battute e uno
stacco di quattro battute senza batteria; poi li fa passare per **cassa
dell'iPad → stanza → microfono dell'iPad**, taglio sotto i 250 Hz compreso: la
cassa dell'iPad la fondamentale del kick non la riproduce, quindi alla rete non
arriva. Sopra ci gira il motore intero, BeatNet ONNX compreso.

`scripts/probe_activations.cpp` registra le attivazioni della rete su quegli
stessi brani e `scripts/probe_replay.cpp` le rigioca dentro al solo
`BeatDecoder`: stesso segnale, stesse misure, un secondo invece di due minuti,
che è ciò che ha reso possibile tarare le decisioni invece di indovinarle.

| Su 30 brani a tempo fisso | prima | dopo |
|---|---|---|
| Battute che ripartono prima del quattro | 268 / 254 / 260 | **0 / 0 / 1** |
| Brani col BPM instabile (span > 1.5 BPM a regime) | 28 / 28 / 29 | **15 / 17 / 18** |
| Span medio del BPM a regime | 21.8 / 24.0 / 25.7 BPM | **10.8 / 11.3 / 13.0 BPM** |
| Salti di BPM > 1 | 312 / 301 / 334 | **125 / 150 / 156** |
| Brani lenti a stabilizzarsi (> 12 s) | 28 / 28 / 28 | **15 / 17 / 19** |
| Ottava sbagliata | 4 / 4 / 4 | 4 / 5 / 5 |
| Tempo fino a FOLLOWING | 2.3 s | 3.9 s |

Tre esecuzioni per colonna: la sonda guida il motore più veloce del tempo reale
e il worker neurale sta su un altro thread, quindi lo scheduling sposta qualche
caso da un run all'altro. Vanno lette come ordine di grandezza.

### Cause, tutte misurate

1. **Il livello metrico veniva scelto sui primi tre secondi.** Ripiegando
   l'attivazione BeatNet di un mix a 104 BPM sul periodo vero, il punto a mezzo
   battito vale **0.86** del battito nelle prime tre battute e **0.15** dopo
   dieci secondi: la LSTM si sta ancora scaldando e tre periodi sono troppo
   pochi per separare un bin sul battito da uno sul controtempo. In quella
   finestra il tempo vero viene *addebitato* di essere un'ottava troppo lento e
   vince il doppio. Ora il fold chiede cinque periodi (`kMinPeriods`), e niente
   è un livello «deciso» prima di nove secondi di buffer.
2. **L'isteresi difendeva il livello sbagliato fino a diciotto secondi.** Il
   seggio si cambiava solo dopo dieci refresh consecutivi, qualunque fosse il
   distacco — e siccome la *salience* riportata è il punteggio di chi siede,
   scendeva a 0.06 mentre il rivale stava a 1.00. Sotto la soglia di salience il
   decoder ignora del tutto il fold, quindi non vedeva nemmeno il disaccordo.
   Ora l'attesa dipende dal distacco: un rivale che stravince entra in due
   refresh. **Solo finché il livello è provvisorio**: una volta deciso serve
   l'attesa piena, altrimenti su materiale ambiguo i due livelli se lo passano
   ogni due secondi.
3. **La battuta veniva riportata all'uno dall'ultimo downbeat arrivato.** La
   rete mette sull'uno vero solo una *pluralità* dei suoi downbeat: ogni
   downbeat sbagliato faceva ripartire la battuta a metà. Ed era uno **snap di
   fase**, che buttava via lo stato dell'anello — errore di fase, trim misurato
   — per una correzione che riguardava solo il conteggio. Ora la battuta si
   corregge dallo **stesso voto** già usato per entrare, con maggioranza netta e
   una pausa di quattro battute fra una correzione e l'altra, e **ruota
   l'indice** invece di spostare la fase: non muove niente e non perde un
   impulso.
4. **Un tempo fisso non veniva rifinito, veniva inseguito.** Il regime si
   decideva su otto battute *consecutive* d'accordo fra il fit a 8 e quello a
   24: col microfono in stanza una battuta storta è normale e un contatore che
   qualunque di esse azzera non arriva mai in fondo, quindi il decoder restava
   in regime «vivo» a inseguire il fit a otto battute. Ora si decide sullo
   **spread del fit lungo** su una finestra di battute (una battuta storta
   allarga lo spread e poi esce), e una volta fisso il tempo converge verso la
   **media corrente** del fit lungo — con una banda morta di 0.05 BPM. È la
   differenza fra «rifinire» e «inseguire»: sulle attivazioni sintetiche lo
   span a regime è **0.00 BPM** su nove tempi da 62 a 186.
5. **La via d'uscita rapida dal tempo fisso scattava sul rumore.** Era la
   mediana di tre intervalli contro il tempo tenuto, tre volte nella stessa
   direzione: il jitter d'attacco di un mix in stanza la supera parecchie volte
   al minuto. Ora una deviazione piccola conta solo se la finestra da 24 battute
   pende dalla stessa parte, mentre una grande sta in piedi da sola (un cambio a
   gradino la finestra non può confermarlo: appena la griglia è sbagliata il
   gate on-grid smette di ammettere battute). Il gradino 120 → 132 resta
   agganciato in **3.6 s**, l'accelerando 120 → 140 resta entro **0.085 di
   battito** dalla pulsazione.
6. **Fra il 3% e un quarto d'ottava non era compito di nessuno.** Un click a 78
   BPM veniva seguito a 90 e ci restava: `log2(90/78)` è 0.21, sotto la soglia
   d'ottava, quindi il ri-aggancio non scattava mai — e il fit da solo non ne
   esce, perché su una griglia sbagliata del 15% sta interpolando quelle battute
   che per caso cadono dentro la tolleranza. Ora un fold deciso *tira* il tempo
   commesso verso di sé senza buttare via niente. Lo stesso click ora sta a
   **78.00 BPM, ±1 ms dalla pulsazione**.

### Cosa resta storto

- **Sotto ~92 BPM con ottavi pieni raddoppia ancora.** Sui brani di prova a 76
  BPM la correlazione a mezzo battito vale 0.73–0.77 contro 0.02–0.18 a 104 e
  128: gli ottavi sono forti quanto i battiti e 152 è una lettura difendibile.
  Ho provato a spostare la banda «troppo veloce» per farlo cadere dalla parte
  giusta: peggiora, perché la stessa asimmetria è ciò che impedisce di leggere
  in half-time un normale backbeat rock. Misurato, non ipotizzato — resta com'è.
- Il half-time a 104 continua a discutere fra 104 e 52. È la stessa ambiguità
  vista da vicino: con rullante solo sul tre, 52 *è* una lettura in half-time.
- Il tempo fino a FOLLOWING è passato da 2.3 a 3.9 s, e su un click lento
  arriva a ~8 s. È il prezzo dichiarato del punto 1: prima si agganciava in due
  secondi all'ottava sbagliata e ci metteva mezzo minuto a uscirne.

### Test aggiunti

- `bar-integrity` — una rete finta sbaglia il downbeat una battuta su due, come
  fa quella vera; la battuta deve arrivare al quattro prima di ripartire. Sul
  codice vecchio: 12 ripartenze anticipate.
- `fixed-hold` — tempo fisso con 22 ms di jitter d'attacco, una battuta su
  dodici mancata e un fill ogni otto battute. Vecchio: span 1.67 BPM, regime
  tenuto il 61% del tempo. Nuovo: **0.64 BPM, 85%**.
- Il conteggio dei cambi di livello ora parte da quando l'estimatore dichiara il
  livello **deciso**, non dalla prima lettura: rivedere un livello provvisorio è
  il comportamento voluto, non un difetto.

## Shaker e congas interrotti (19 agosto)

Cinque difetti distinti, tutti nel percorso di riproduzione:

1. Ogni campione sintetizzato era un decadimento **troncato**: shaker al 21 % del picco, conga aperta all'11 %, slap all'8 %. Un click a **ogni** colpo. Ora ogni campione ha una dissolvenza finale di 12 ms.
2. Una voce sostituita dal colpo successivo veniva spenta a metà campione — ora sfuma in 4 ms.
3. Con tutte le voci occupate l'allocazione ripiegava sullo **slot 0**, sovrascrivendo una voce in suono. Ora prende la più vecchia; le voci sono 16.
4. La guardia anti-ritrigger era l'82 % di un impulso calcolato sul BPM **visualizzato** (120 finché non c'è lock): sopra ~145 BPM reali era più lunga dell'impulso vero e **buttava via un colpo su due**. Ora è una guardia fissa di 20 ms, e il groove prende il tempo dal clock.
5. `pulseBeatInBar` era sbagliato per gli impulsi dopo un confine di battito dentro lo stesso blocco, e il tumbao sceglie la conga da quell'indice: suonava il tamburo sbagliato.

## Limiti noti dopo questo giro

- Sopra ~168 BPM e sui groove in **half-time** il tracker preferisce il livello lento (84 invece di 168). Per un orologio di percussioni è spesso la lettura più utile, ma è una scelta, non una certezza.
- Sotto ~92 BPM con ottavi pieni può ancora raddoppiare — vedi la sezione del 19 agosto sera, dove è misurato e spiegato perché non si corregge spostando le soglie.
- Restano numeri su materiale **sintetico**, per quanto il percorso cassa → stanza → microfono sia simulato: la prova sul device con Spotify vero è ancora da fare.

## Cosa locka oggi

Il modello è BeatNet addestrato su **musica** (GTZAN), non sul click sintetico.

| Sorgente | Atteso |
|---|---|
| Kit / groove con kick+snare (test host) | Lock automatico senza TAP |
| Spotify in SPEAKER (casse iPad, microfono) | Modo **IPAD**. Volume alto aiuta; aspetta FOLLOWING. CLICK TEST **spento** |
| CLICK TEST | Kick+snare+hat a 120. Può lockare; se no, TAP |
| Piano / ambient / rubato | Può restare in LISTENING o sbagliare l’ottava. TAP resta il fallback |

SPEAKER: l’app **non** legge l’audio digitale di Spotify. Sente il microfono.

Se la riga AI dice **AI STUB**, il `.onnx` non è nel binario: riconfigura iOS.

## Come provare Spotify

1. Modo **IPAD** (non MIXER)
2. CLICK TEST **off**
3. Spotify in play, volume alto
4. Controlla: **AI ONNX**, **MIC LIVE**, `nn` che si muove, poi **FOLLOWING**
5. **START** sul downbeat
6. Se `nn --` e `p 0.00` dopo 10 s: il mic non sente il mix (AEC / troppo lontano). Avvicina l’iPad o alza Spotify
7. TAP resta il fallback

## Installare sull’iPad

```bash
./scripts/setup-ai.sh
./scripts/configure-ios.sh
open build-ios/VirtualPercussionist.xcodeproj
```

In Xcode: team `28H5MJ7244`, iPad Air M1, Run, microfono. Primo avvio: CoreML può compilare il grafo qualche secondo.

## Runtime

- Worker (non audio thread): resample 22.05 kHz → BeatNet LSTM → decoder → PLL
- START aspetta il **primo quarto** (downbeat). BeatNet allinea la battuta 1.
- Shaker **monofonico** (un grain per 8ª, niente overlap a tempo stabile) + riff di **congas** (tumbao: tumba / open / slap)
- Follow strength: high. Griglia: ottavi
- CoreML su iPad; se l’EP rifiuta, ORT ricade sulla CPU
- Attribuzione: Heydari, Cwitkowitz, Duan — BeatNet, ISMIR 2021, CC BY 4.0

## I comandi sullo schermo

Dal 19 agosto l'interfaccia espone tutto quello che il motore sa fare. Prima
`grooveStyle`, `grooveAuto`, `congasEnabled`, `swing` e `intensity` esistevano nel
C++ ma non erano raggiungibili: l'app suonava sempre la marcha, e tre parti su
quattro erano intestabili.

| Comando | Cosa fa |
|---|---|
| **AUTO / MARCHA / ROCK / DANCE / POP** | la parte che suona. AUTO lascia scegliere allo `StyleDetector`; premere una parte a mano spegne AUTO |
| **SHAKER / CONGAS** | accendono e spengono i due strumenti separatamente |
| **SOURCE (IPAD / MIXER)** | microfono in stanza contro mic ravvicinato |
| **1/4 · 1/8 · 1/16** | griglia dello shaker |
| **SWING** | 0 = dritto, 1 = terzinato. Da alzare sugli shuffle |
| **ENERGIA** | quanto forte suona il percussionista |
| **REVERB** | ambiente |

La riga **PARTE** sotto il BPM dice sempre quale parte sta suonando; con AUTO
attivo mostra anche la confidenza del rilevatore e tinge di verde il pulsante che
ha scelto. Le due manopole SWING ed ENERGIA compaiono in verticale; in
orizzontale lo schermo non le contiene e vengono nascoste.

## Checklist device

Base — deve funzionare:

- [ ] Riga UI: AI ONNX + IPAD (non STUB, non MIXER)
- [ ] CLICK TEST → FOLLOWING, shaker dopo START
- [ ] Spotify SPEAKER → FOLLOWING senza TAP, shaker dopo START

Tempo e battuta — è qui che si vede il giro del 19 sera. Il pannello **DBG**
ora ha la riga `tempo FISSO/VIVO/CERCO · livello deciso/provvisorio · fold nnn`:
è la prima cosa da leggere quando qualcosa non torna.

- [ ] Su un brano registrato in studio il BPM deve **fermarsi** e restare fermo.
      Aspettati che si assesti in 5–10 s e che poi non si muova più di qualche
      decimo. Se oscilla di un BPM o più, segna la riga DBG
- [ ] La battuta deve arrivare al **quattro** prima di tornare all'uno. Guarda
      il pallino, non l'orecchio: è il difetto «uno, due, uno»
- [ ] Quando trova il tempo giusto non deve poi scappare via: se lo vedi salire
      o scendere in fretta dopo essersi assestato, segna `tempo` e `fold`
- [ ] Brano lento (sotto ~92) con ottavi pieni: **atteso che raddoppi**. Segna
      se lo fa e a quale tempo
- [ ] USB-C + mic kit
- [ ] SPEAKER senza auto-inseguimento dello shaker
- [ ] Accelerando / rallentando
- [ ] Stacca/riattacca interfaccia
- [ ] Interruption / background

Le quattro parti — nuove, mai provate sul device:

- [ ] MARCHA su un brano latino
- [ ] ROCK su un brano con backbeat
- [ ] DANCE su qualcosa a quattro in piedi
- [ ] POP dove le altre tre sono troppo
- [ ] Cambio di parte mentre suona: non deve saltare né perdere la battuta
- [ ] SHAKER da solo (CONGAS off) e CONGAS da sole (SHAKER off)
- [ ] AUTO: che parte sceglie, e quanto spesso cambia idea. **Atteso: sbaglia.**
      Misurato a 3 casi su 9, cioè come tirare a indovinare — è per questo che è
      spento di default. Serve sapere *come* sbaglia, non se sbaglia

Suono e feel:

- [ ] SWING alto su uno shuffle: lo shaker deve andare con il brano, non contro
- [ ] ENERGIA da 0 a 1: deve cambiare peso, non solo volume
- [ ] Fill sull'ottava battuta: si sente? disturba?
- [ ] Colpi ripetuti veloci (1/16 a tempo alto): nessun click, nessun buco

Cosa segnare quando qualcosa non va: BPM mostrato, riga PARTE, stato, e il
pannello **DBG** in alto a destra, che ora riporta anche le cinque misure su cui
il rilevatore di stile decide.

## Cosa non è in questo MVP

- Particle filter completo di BeatNet (usiamo `BeatDecoder` + PLL)
- Signalsmith (loop TSM; shaker live a grain)
- Android / Oboe
- MIDI, Link, editor pattern
- Licenza JUCE commerciale (serve per App Store closed-source)

## File modello

| File | Ruolo |
|---|---|
| `Assets/Models/beatnet.onnx` | Pesi BeatNet (CMake embedda al configure) |
| `scripts/export_beatnet_onnx.py` | Scarica `model_1_weights.pt` e esporta ONNX |
| `scripts/train_export_beat_model.py` | Vecchio TCN-click, non è più il default |
| `Source/AI/LogSpectFeatures.*` | Feature allineate a BeatNet |
| `Source/AI/ModelLocator.cpp` | LSTM on, I/O `features`/`logits`/`h0`/`c0` |

## Comandi

```bash
./scripts/run-tests.sh                 # host
./scripts/configure-ios.sh             # re-embed + Xcode proj
./scripts/build-simulator.sh
```

Diagnostica del tracking (host, non fanno parte della suite — sono lenti):

```bash
cmake --build build-host --target VPProbe        # 30 brani a tempo fisso, motore intero
./build-host/VPProbe_artefacts/Release/VPProbe
./build-host/VPProbe_artefacts/Release/VPProbe --trace straight 104   # un caso solo

cmake --build build-host --target VPActivations VPReplay
./build-host/VPActivations_artefacts/Release/VPActivations 104 straight > act.txt
./build-host/VPReplay_artefacts/Release/VPReplay act.txt              # solo il decoder
./build-host/VPReplay_artefacts/Release/VPReplay --levels act.txt     # la lite sulle ottave
```

`VPProbe` vuole ONNX Runtime host (`./scripts/fetch_onnxruntime.sh`): senza, gira
lo stub e non misura niente di utile.
