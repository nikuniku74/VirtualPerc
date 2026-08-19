# Stato dell’app — Virtual Percussionist

Documento operativo. Aggiornarlo ogni volta che cambia cosa si può installare, cosa locka, e cosa resta da provare sul device.

**Data:** 19 agosto 2026  
**Target:** iPadOS 16+, iPad Air M1  
**Versione albero:** 0.1.0

## Pronto per l’iPad

Sì: apri il progetto Xcode, seleziona l’iPad, Run. Nel binario c’è **BeatNet** (pesi GTZAN) + ONNX Runtime iOS (CPU + CoreML).

Dopo questo cambio **riconfigura** iOS (`./scripts/configure-ios.sh`) e reinstalla: il lock SPEAKER/Spotify è cambiato nel C++.

## Ultima verifica automatica (18 agosto 2026)

| Check | Esito |
|---|---|
| Host `VPTests` | 42 passed: kit/CLICK/quiet SPEAKER 120.0 stabile; sincopi forti senza oscillazione; START esce da ATTENDO BATTUTA |
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

## Shaker e congas interrotti (19 agosto)

Cinque difetti distinti, tutti nel percorso di riproduzione:

1. Ogni campione sintetizzato era un decadimento **troncato**: shaker al 21 % del picco, conga aperta all'11 %, slap all'8 %. Un click a **ogni** colpo. Ora ogni campione ha una dissolvenza finale di 12 ms.
2. Una voce sostituita dal colpo successivo veniva spenta a metà campione — ora sfuma in 4 ms.
3. Con tutte le voci occupate l'allocazione ripiegava sullo **slot 0**, sovrascrivendo una voce in suono. Ora prende la più vecchia; le voci sono 16.
4. La guardia anti-ritrigger era l'82 % di un impulso calcolato sul BPM **visualizzato** (120 finché non c'è lock): sopra ~145 BPM reali era più lunga dell'impulso vero e **buttava via un colpo su due**. Ora è una guardia fissa di 20 ms, e il groove prende il tempo dal clock.
5. `pulseBeatInBar` era sbagliato per gli impulsi dopo un confine di battito dentro lo stesso blocco, e il tumbao sceglie la conga da quell'indice: suonava il tamburo sbagliato.

## Limiti noti dopo questo giro

- Sopra ~168 BPM e sui groove in **half-time** il tracker preferisce il livello lento (84 invece di 168). Per un orologio di percussioni è spesso la lettura più utile, ma è una scelta, non una certezza.
- Sotto ~64 BPM con ottavi marcati può ancora raddoppiare.
- Restano numeri su materiale **sintetico**: la prova sul device con Spotify vero è ancora da fare.

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
