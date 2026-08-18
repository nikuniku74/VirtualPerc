# Stato dell’app — Virtual Percussionist

Documento operativo. Aggiornarlo ogni volta che cambia cosa si può installare, cosa locka, e cosa resta da provare sul device.

**Data:** 18 agosto 2026  
**Target:** iPadOS 16+, iPad Air M1  
**Versione albero:** 0.1.0

## Pronto per l’iPad

Sì: apri il progetto Xcode, seleziona l’iPad, Run. Nel binario c’è **BeatNet** (pesi GTZAN) + ONNX Runtime iOS (CPU + CoreML).

Dopo questo cambio **riconfigura** iOS (`./scripts/configure-ios.sh`) e reinstalla: il lock SPEAKER/Spotify è cambiato nel C++.

## Ultima verifica automatica (18 agosto 2026)

| Check | Esito |
|---|---|
| Host `VPTests` | 30 passed: kit/CLICK/quiet SPEAKER 120.0 stabile; sincopi forti senza oscillazione; START esce da ATTENDO BATTUTA |
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

## Checklist device

- [ ] Spotify SPEAKER → FOLLOWING senza TAP, shaker dopo START
- [ ] Riga UI: AI ONNX + IPAD (non STUB, non MIXER)
- [ ] CLICK TEST → FOLLOWING, shaker dopo START
- [ ] USB-C + mic kit
- [ ] SPEAKER senza auto-inseguimento dello shaker
- [ ] Accelerando / rallentando
- [ ] Stacca/riattacca interfaccia
- [ ] Interruption / background

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
