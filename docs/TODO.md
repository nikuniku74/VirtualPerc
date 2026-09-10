# TODO integrazione

Spunta `- [x]` quando un pezzo è integrato e verificato.  
Se scopri qualcosa di nuovo, aggiungi una riga. Se un item cambia, aggiorna il testo, non cancellarlo in silenzio.

**Ordine:** 1 = più importante (senza questo il resto non tiene). Non riordinare per recenza.

**Fuori da questo elenco (standby):** loop registrati (`docs/HANDOFF_LOOP_DEBUG.md`); chiusura formale ciclo Codex PATTERN — sezione **Standby** in fondo. Mic iPad / speaker in stanza: non è il path di verifica (si testa **brano caricato** e **mixer**).

**Come verificare:** per DSP/tempo, un numero di probe o un test; per musica, ascolto. Non dichiarare chiuso un item musicale senza orecchio.

---

## In corso / da fare

### 1. Tempo lento (~50 BPM): cassa / rullo / charleston a ottavi 🟡 (2026-09-04, indagine chiusa: indecidibile dall'audio, mitigato con TAP e ÷2/×2 — resta ascolto)

A tempi lenti (es. **50 BPM**) il groove da batteria è chiaro: **cassa sull'1 e sul 3**, **rullo sul 2 e sul 4**, **charleston (hi-hat) a ottavi**. L'app **non tiene bene il tempo**, anche se il charleston dà un pulse ogni ottavo — non è un brano "vuoto".

Sospetti (skill tempo: sotto ~100 BPM il fold legge gli ottavi come beat; range 40–220; PLL con quarto da 1,2 s): ottava 50 vs 100, fase che deriva, lock che non si chiude. Non "sistemare" bloccando sempre su 100. Il clock non riparte perché il BPM è basso.

**Stato al 2026-09-04 (sessione ChatGPT, crediti finiti a metà — verificato indipendentemente da Claude sullo stesso albero):**

- [x] Riprodotto e diagnosticato: **non è deriva del PLL**. Il decoder resta stabilmente sul livello metrico degli ottavi (legge gli hat come battito, non la cassa). Confermato con probe mirato: 50 BPM mixer → letto ~100 BPM (ottava 0, sbagliato); 50 BPM file → ~100 BPM (sbagliato); 100 BPM mixer/file → corretto in entrambi, come controllo.
- [x] **Prima versione di fix, insufficiente** — già nel codice, non toccarla via reset: `BeatDecoder::observeDownbeatCadence()` (`Source/AI/BeatDecoder.cpp`/`.h`) conta la distanza fra downbeat che superano una soglia "forte"; se due bar consecutivi misurano 8 battiti invece di 4, propone `metricalOctaveHint` di un'ottava sotto. Collegato fino a `BeatTracker::updateAutoOctave` (`Source/Tracking/BeatTracker.cpp`/`.h`, che ora accetta l'hint) e `kOctaveTooSlow` abbassato 76→49. **Non basta**: il modello reale a 50 BPM non produce downbeat "forti" discreti abbastanza spesso perché il contatore raccolga i due bar consecutivi richiesti — il segnale di battuta del modello è soprattutto un valore continuo su ogni beat, non un evento raro sopra soglia. Verificato di nuovo da Claude il 2026-09-04 con `VPProbe --trace --mixer plain 50` (build pulita sull'albero corrente): stesso esito, 50→~100 BPM ancora sbagliato.
- [x] **Corretto (Claude, 2026-09-04) usando la probabilità continua, come indicato** — ma **non basta neanche questo**, e il motivo è musicale, non un bug di implementazione. Riscritto `observeDownbeatCadence()`: istogramma a 8 posizioni sulla confidenza continua di downbeat, indicizzato dal **tempo trascorso** diviso il periodo corrente (non da un contatore di battiti accettati — misurato che la griglia salta occasionalmente un ottavo anche su segnale pulito, e un contatore a incrementi si sfasa per sempre dopo ogni salto; l'indicizzazione a tempo assorbe il salto, come già fa il fit del tempo altrove nel file). Test: bin dominante vs bin opposto (4 posizioni via) debole = livello sbagliato.
  - **Causa per cui non funziona su questo materiale:** misurato con `VPProbe` che la curva di downbeat della rete attiva **comparabilmente sia sul vero battere 1 che sul vero battere 3** — la stessa identica ambiguità 1-vs-3 già annotata nell'item 2 ("1 vs 3 resta l'ambiguità"). A ottava sbagliata, battere 1 e battere 3 cadono esattamente a 4 posizioni di distanza nell'istogramma a 8: la stessa identica "firma" che il test usa per dire "livello già corretto". Il segnale di downbeat da solo **non può distinguere** le due ipotesi su questo materiale.
  - **Alternativa provata e falsificata:** l'ampiezza generale di attivazione (beat, non downbeat) non alterna debole/forte fra ottavi on-beat e off-beat come ci si aspetterebbe (misurato: resta alta 0.5–0.98 su quasi ogni ottavo) — la rete tratta gli ottavi dell'hi-hat come impulsi salienti quanto cassa/rullo, quindi neanche l'ampiezza aiuta.
  - **Pista armonia (item 2, `HarmonicChange`/`barFromHarmony`) scartata**: il materiale del bug è **batteria pura** (cassa/rullo/hi-hat, nessun accordo) — non c'è alcun contenuto armonico da cui `HarmonicChange` possa estrarre un cambio. Questa pista non si applica a questo bug specifico.
  - **Conclusione onesta:** con i soli tre scalari che il decoder riceve dalla rete (pBeat, pDownbeat, pNone) non ho trovato un discriminante statistico affidabile per questo materiale. Risolverlo per davvero richiederebbe probabilmente o (a) una nuova feature con accesso al contenuto spettrale grezzo (bassa frequenza di cassa/rullo vs hi-hat, che il decoder oggi non riceve), o (b) la stessa scala di validazione empirica (molti brani/stili) già usata per tarare il resto di questo file — non un correttore isolato. Non riprendere questa via senza uno di questi due investimenti.
  - Codice lasciato nel tree: l'indicizzazione a tempo è comunque un miglioramento reale rispetto al contatore fragile di prima (non introduce regressioni, verificato con `VPProbe --trace --mixer plain 50/100/132`), anche se il correttore non scatta ancora su questo materiale.
- [x] **Feature spettrale provata (Claude, 2026-09-04) — e qui l'item cambia natura: il caso è indecidibile dall'audio.** Piano, misure e numeri in `**docs/HANDOFF_OCTAVE_50BPM.md`** (leggerlo prima di riprovare qualsiasi cosa qui).
  - Portata fino al decoder l'energia delle bande basse del frame che già alimenta la rete (`LogSpectFeatures::lowBandEnergy`, 24 bande, ~30–250 Hz: cassa e rullo hanno corpo lì, l'hi-hat no). Test: profondità dell'alternanza fra le due classi di battiti accettati.
  - **Sul caso target funziona:** 50 BPM passa da 100.02 a **50.03**, e 76/100/118/132/140 + stili syncopated/pad/sync+pad restano invariati. Divario misurato netto: profondità 0.43 (lettura sbagliata) contro 0.85 (lettura giusta).
  - **Ma rompe il half-time:** materiale half-time a 100 BPM (rullo solo sul 3) passa da 99.92 a 60.13 instabile. A/B fatto: è il correttore. E **non è una soglia da spostare**: in half-time le posizioni 2 e 4 sono vuote in banda bassa esattamente come gli ottavi dell'hi-hat a 50 — *stessa spaziatura, stesso basso sotto gli stessi slot*. Una battuta straight a 50 e una half-time a 100 **sono lo stesso suono**; quale dei due si chiami "il tempo" è una convenzione, non un dato acustico.
  - **Correttore lasciato nel tree ma spento** (`kCadenceCorrectionEnabled = false` in `BeatDecoder.cpp`, con i numeri nel commento) insieme al banco che l'ha misurato, così chi riprende rimisura con un comando invece di ricostruire tutto.
- [x] **Conseguenza, decisa dall'utente il 2026-09-04: rimessi i ÷2/×2.** Se l'audio non può decidere, serve qualcosa da fuori: TAP c'era già, e ora ci sono di nuovo anche i due pulsanti (item 15, ripristinati e testati end-to-end). **Questo non "chiude" l'item 1**: la lettura automatica a 50 BPM resta sbagliata: quello che cambia è che ora il musicista ha come rimediare in un gesto. I quattro test RED dell'altra sessione (`mixer/file slow kit holds 50 BPM…`) restano rossi apposta, e vanno letti così: descrivono un obiettivo che sappiamo non raggiungibile per via automatica.
- [x] Verifica fatta con strumenti mirati (`VPProbe --trace --mixer <stile> <bpm>`, pochi secondi l'uno) su tutta la matrice 50/76/100/118/132/140 × straight/syncopated/pad/sync+pad/half-time, non con la suite intera. Vedi item 16 e la nota in fondo al file.
- [x] Docs in `.claude/skills/realtime-tempo/SKILL.md` — scritto: §2 "For one class of material it is not *partly* unsolvable - it is undecidable", con le tre vie tentate, i numeri e la tabella che mostra perché straight-a-50 e half-time-a-100 sono lo stesso segnale. Sostituisce la vecchia riga che dava per scontato che i casi ambigui si risolvessero nel tracker.
- [x] Gate finale `VPTests` intera: lanciata su richiesta esplicita dell'utente (vedi esito in fondo all'item).
- [ ] **Ascolto** — nessun orecchio umano su questo item finora. È l'unica verifica rimasta che non posso fare io: serve sentire un brano lento reale (mixer o file caricato) e dire se la lettura a ottava doppia è davvero il problema che si sente, o se con il TAP il caso è già coperto in pratica.

---

### 2. Capire qual è il primo quarto (e riallinearlo dopo un taglio) ✅ (2026-09-07)

L'app deve mettere l'**1** sul primo quarto del 4/4 su mixer e brano caricato, senza «L'1 è QUI». Se il musicista **taglia due quarti** e riprende in 4/4, deve riallineare l'1 in poche battute. Niente look-ahead; il clock del tempo non riparte; ruotare l'1 = solo `rotateBarIndex`.

Chiuso: istogramma downbeat + armonia (`BeatTracker::alignBarFromVotes`). In play servono **47 battiti accettati** (~12 battute a 4/4, cioè 23,6 s a 120 BPM e 47,2 s a 60) prima di ruotare — vedi la tabella nell'item 22; qui c'era scritto «~8 battute», che era sbagliato. Un buco di due quarti apre una finestra coming-in di 4 battute (`notifyBarReentry`, 8 beat di evidenza, una rotazione); il decoder **non** riparte. `barLocked` **vieta** ogni rotazione automatica. Sul percorso speaker c'era un gate circolare: l'allineamento veniva chiamato solo se `barFromHarmony` era già vero, ma quel flag può diventare vero soltanto dentro l'allineamento. Corretto chiamando sempre l'allineamento, saltando invece il solo istogramma neurale (misurato a caso sul ritorno acustico) e lasciando rispondere l'armonia. Su materiale che non contiene alcun indizio affidabile di battuta l'1 non è deducibile: in quel caso resta intenzionalmente `SPOSTA L'1`, non una rotazione casuale.

- [x] **Baseline:** il banco precedente ha misurato ~20–21/25 sul line feed e voto vicino al caso sul ritorno acustico; la segnalazione d'ascolto ha esposto il percorso armonico irraggiungibile, non una soglia da allentare.
- [x] **Test RED taglio:** `VPTests --bar` — following, tace 2 quarti, downbeat del modello su quello che il clock chiama 3. Entro 4 battute `beatInBar` è 0; decoder `analysisRestarts` invariato. Fill senza buco di livello non ruota. Con lucchetto non ruota. Seek: stessa finestra, niente epoch.
- [x] **Finestra di rientro:** `maybeDetectBarReentry` (picco del blocco vs `levelLoud`, non l'epoch da 4 s) + `notifyTrackSeek` → `BeatTracker::notifyBarReentry`. Senza `notifyInputRestart`. Una rotazione. RT: niente alloc/lock/I/O.
- [x] **Ingressi:** file/mixer continuano a usare prima il voto neurale; speaker esclude quel voto e può ora entrare nell'armonia. Il test end-to-end armonico usa esplicitamente `FollowSource::speaker`, così il gate circolare non può tornare.
- [x] **Docs:** `.claude/skills/realtime-tempo/SKILL.md` + `docs/AUDIO_ENGINE.md` (rientro; lucchetto; clap `barTrusted`).
- [x] **Gate rapido:** `VPTests --bar` — 10/10 PASS (taglio, fill, lucchetto, seek). Suite completa non lanciata, come richiesto. Il clap legge `tr.barTrusted`; una prova percettiva su iPad resta uno smoke test utile, non lavoro software aperto.

Piano discusso: Task 0 = baseline; 1 = RED; 2 = finestra rientro; 3 = path file; 4 = docs.

---

### 3. Cambio brano a START già on (60 → 120 non riallinea)

Se carico un brano a es. **60 BPM**, premo **START**, poi carico **un altro** a **120 BPM**, l'app **non si allinea subito**. Se prima premo **STOP**, al nuovo brano **si allinea subito**.

Oggi `loadInternalTrack` sostituisce il file e fa `trackTransport.start()`, ma **non** resetta il tracker (`engine.stop()` / `notifyInputRestart`). Resta il lock del 60 sul pezzo a 120.

Un file nuovo è un **ingresso nuovo**, non una deriva del brano precedente. L'utente non deve dover premere STOP. START può restare acceso; BPM e fase devono chiudersi sul B con la stessa prontezza di uno START pulito. Il clock non "riparte" a caso a ogni BPM: è il cambio di sorgente che va dichiarato.

- [ ] Riprodurre: START su file A (60) → CARICA file B (120), senza STOP. Oggi: tempo/fase del A. Con STOP in mezzo: lock rapido sul 120.
- [ ] Al load (e analogo seek, item 14): nuovo ingresso (restart decoder / finestra coming-in), non continuare il PLL del A. RT: niente alloc.
- [ ] Percussioni sul B in poche battute, non restare a 60 per 8–20 s.
- [ ] Test: A 60 → B 120 e A 120 → B 60, senza STOP; BPM e fase del secondo file. Ascolto.
- [ ] Docs skill tempo: cambio file ≠ taglio in-song (item 2).

---

### 4. Drift guard (muto se è esageratamente fuori tempo)

Se la parte è **esageratamente fuori tempo** rispetto al brano, **non deve suonare nulla** e deve **riprendere appena rientra** sul tempo. È una **guardia**, non un riallineamento: non sposta l'1 e non cambia il BPM; tace e riattacca.

**UI (impostazioni):** toggle **Drift guard**, **acceso di default**, disattivabile. Sotto, una riga di descrizione, es.:

> Se il tempo stimato è troppo lontano dal brano, le percussioni tacciono e ripartono da sole quando il lock è di nuovo solido.

Soglia "esageratamente" da misurare (fase, confidenza, stato `following` vs `lowConfidence` / `recovering`). Non confondere con IN ASCOLTO / `BandDynamics` (quello è il volume della band, non lo scarto di fase). A 50 BPM (item 1) la guardia non deve mutare un lock lento-ma-giusto.

- [ ] Definire il criterio (fase oltre X ms e/o lock perso) e l'isteresi del rientro, così non batte on/off.
- [ ] Setting `driftGuard` (o nome affine) default **on**; audio thread legge solo atomic.
- [ ] Mute della parte (shaker, congas, clap, cembalo) senza fermare tracker/clock; phrase count continua a correre (come già per il mute).
- [ ] UI: toggle + descrizione breve in impostazioni.
- [ ] Test: fuori tempo → silenzio; rientro → suona; toggle off → continua a suonare anche storto.
- [ ] Ascolto: un dropout breve, non un buco di battute dopo che è già riallineato.

---

### 5. Cambio parte (style) → esce dal tempo ✅ (2026-09-07, non riprodotto)

Segnalazione: cambiando lo stile delle percussioni sembrava che la parte perdesse il tempo. Il cambio è stato isolato dal normale movimento della rete con `VPOps --style-change`: non muove il clock. Sul brano caricato il delta contro la corsa di controllo è **−0,00 ms**; sul percorso iPad **+1,93 ms**, sotto la risoluzione di circa 10 ms del banco. Sul mixer le corse indipendenti della rete non sono deterministiche (una seconda corsa di controllo è uscita da sola fino a 254 ms), quindi non si attribuisce quella deriva al pulsante. Il nuovo stile cambia accenti e buchi del pattern e può dare una diversa impressione metrica senza che la griglia si sia spostata.

- [x] Riproduzione mirata: MARCHA → ROCK, 118 BPM, START già on, brano diretto / iPad / mixer.
- [x] Traccia: `setGrooveStyle` scrive solo `requestedStyle`; `applyPendingGrooveControls` committa sul successivo quarto. Non chiama tracker, clock, `alignPhrase`, reset o re-anchor.
- [x] Verifica rapida: `VPOps --style-change [--file|--mixer] --bpm 118`. Brano −0,00 ms; iPad +1,93 ms; nessun gap/restart attribuibile al cambio. Il test unitario esistente verifica inoltre che stile e swing aspettino il quarto successivo.
- [x] Esito: nessun fix al motore necessario. Se ricompare all'ascolto, annotare stile di partenza/arrivo e sorgente: va confrontato il pattern percepito o la deriva del materiale, non riaperto il clock senza una misura sopra il rumore del banco.

---

### 6. Pulsante AUTO (STRUMENTI) — densità che segue l'ottava del tempo

In **STRUMENTI**, **AUTO** oggi è solo un alias di **1/8**: non ascolta il brano e non cambia mai griglia. Deve diventare la modalità che fa quello chiesto sul raddoppio/dimezzamento.

**Manuale (fisso):** `1/4`, `1/8`, `1/16` restano quello che premi. Non si auto-adattano. Se il BPM raddoppia con 1/16 acceso, la parte resta a sedicesimi (e suona più veloce): è una scelta esplicita.

**AUTO:** tiene la **densità percepita** quando il tempo **raddoppia o dimezza** (ottava metrica). Copre tutte e tre le griglie, spostandosi tra loro. Il clock resta a sedicesimi; si cambia solo il thinning in `GrooveEngine`. Niente restart del clock, niente look-ahead.

Tabella (ottava 0 = lock "giusto", densità di riposo = **ottavi**, come oggi):


| Ottava del tempo          | Griglia che suona |
| ------------------------- | ----------------- |
| −1 (BPM circa **metà**)   | **1/16**          |
| 0                         | **1/8**           |
| +1 (BPM circa **doppio**) | **1/4**           |


Così: se AUTO era sugli ottavi e il tempo raddoppia, non resta a 1/8 sul BPM doppio (troppo fitto) → passa a **quarti**. Se dimezza → **sedicesimi**, così non resta vuota. Stessa logica in tutte le direzioni.

Fuori dai salti ×2 / ÷2 (un +5 % di BPM, un fill) **non** deve cambiare griglia.

UI: AUTO acceso = questa logica. `1/4` / `1/8` / `1/16` spengono AUTO. Non confondere con **AUTO in PARTE** (scelta dello stile). Eventuale riga in impostazioni, es.: *«AUTO regola quarti / ottavi / sedicesimi se il tempo raddoppia o dimezza.»*

- [ ] Riprodurre raddoppio/dimezzamento (octave auto vs lock sbagliato) e distinguere i due casi.
- [ ] Implementare **solo** con `subdivision == autoDetect`; mappare ottava → 1/4, 1/8 o 1/16 nel thinning, senza scrivere sopra la scelta AUTO nei settings.
- [ ] `1/4` / `1/8` / `1/16`: comportamento attuale, invariato.
- [ ] Test: AUTO a ottava 0/±1 dà la tabella; manuale 1/16 a ottava +1 **resta** 1/16; niente switch su fill o deriva piccola.
- [ ] Ascolto: AUTO + tempo che raddoppia non deve suonare "doppio veloce"; dimezzando non deve restare troppo rada.
- [ ] Docs skill percussioni: AUTO non è più "significa ottavi fissi".

---

### 7. Swing: pulsante ON/OFF in STRUMENTI (niente knob) 🟡 (2026-09-07, forma corretta sul riferimento dell'utente + valore scelto d'orecchio — resta ascolto in app)

Oggi lo swing è una **knob** 0–100% (`swingSlider` → `settings().swing` 0..1). Non serve una quantità: o è **dritto** o è **swing**. Togliere la knob; in **STRUMENTI** (accanto a shaker / congas / AUTO / 1/4 / 1/8 / 1/16) un pulsante **SWING** acceso/spento.

**ON** = swing pieno (terzina): l'"&" sta a **due terzi** del quarto (`kFullSwingBeats = 1/6`, `humanDelay` in `GrooveEngine`). **OFF** = 0, griglia dritta. Nessun valore intermedio in UI. Cambio come già per parte/swing: **al prossimo quarto**, non a metà beat.

- [x] UI: pulsante **SWING** in STRUMENTI (decimo quadrato, accanto a NATURALE, stesso `setupBtn` / stesso acceso-fucsia). Knob `swingSlider` + label + valore **via**, FEEL passa da 8 a 7 manopole, `hInst` da `squareFor (9)` a `squareFor (10)`. Prefs: scritte 0/1, ma **lette dal vecchio double**, così un'installazione che aveva la knob a metà torna su "acceso" invece che su un valore che la UI non sa più mostrare (`> 0.5` = swing).
- [x] DSP: `applySwing` scrive `swing = 1` / `0`. **La formula del warp non è stata toccata: era già giusta.** Verificato prima di mettere mano alla UI.
- [x] **Swing fatto per davvero — misurato, non "acceso".** Probe usa e getta su `GrooveEngine::eventsAt` con humanize a 0, tutti e 16 gli step, marcha 1/16:

  | step | dritto | swing pieno | atteso |
  |---|---|---|---|
  | 0/4/8/12 (quarto) | 0,0000 | **0,0000** | 0 |
  | 1/5/9/13 ("e") | 0,2500 | **0,3333** | 1/3 |
  | 2/6/10/14 ("&") | 0,5000 | **0,6667** | 2/3 |
  | 3/7/11/15 ("a") | 0,7500 | **0,8333** | 5/6 |

  16 step su 16 esatti, in entrambi gli stati. `delayBeats ≥ 0` verificato anche a swing 0,25 / 0,5 / 0,75 (mai anticipo). Humanize resta un termine a parte: il probe lo azzera e i numeri sopra sono solo lo swing. Commit al prossimo quarto: test già esistente in `TestMain`, invariato.
- [x] Il probe è stato buttato e l'asserzione vive nella suite: **`swing-grid`** in `Tests/TestMain.cpp`, tre `expect` (dritto dov'è scritto / terzina / mai anticipo). Ha un filtro suo, **`VPTests --swing`**, come `--leak` e per lo stesso motivo: sedici chiamate a `GrooveEngine` non devono stare dietro minuti di worker neurale. Girato: `3 passed, 0 failed`.
- [x] Render per l'ascolto: `VPRender --style marcha --bpm 112 --sub 16 --click --swing 0|1`, mandati all'utente.
- [x] Loop registrati: il path era già corretto e non silenzia — `HybridPercussionRenderer` rende **sempre** il motore a colpi come fallback, quindi con SWING acceso il banco rifiuta (oltre `LoopBank::swingTolerance` 0,18) e si resta sul sintetico. Cambiata solo la scritta in SETUP, che diceva `LOOP (swing massimo 18%)` e si poteva leggere come "il loop sta suonando con al massimo il 18%": ora `PATTERN (SWING: il banco non ha prese swingate)`.
- [x] Docs skill percussioni: §8 dice tasto, non knob, con la tabella della geometria e il rimando a `swing-grid`.
- [x] **Lo swing era della forma sbagliata, e il riferimento dell'utente lo ha dimostrato (2026-09-07).** L'utente ha portato un riferimento — shaker Afrobeats, 106 BPM, `53_SAM4_SHAKER_106BPM - Bbm.wav` — dicendo «l'andamento è questo, è una specie di sincope». Misurato (onset a 1 ms, 24 colpi per posizione, dispersione dei cluster 1–8 ms):

  | modello | errore medio |
  |---|---|
  | dritto | 20,8 ms |
  | **warp del quarto** (quello che c'era: l'"&" a 2/3) | **26,6 ms** — peggio del dritto |
  | **warp dell'ottavo** (0, 1/3, **1/2**, 5/6) | **7,6 ms** ✓ |

  Gli **ottavi restano fermi** (0,516 e 0,484 di quarto, 9 ms dall'even): a muoversi è solo il sedicesimo *dentro* l'ottavo, che cade al 57,4 % e 65,7 % (media **61,6 %**; dritto 50 %, terzina piena 66,7 %). Più un accento 2× su un colpo per quarto. Warpare il quarto fittava quella musica **peggio di non swingare affatto**, perché l'unico colpo che sposta è quello che la musica tiene fermo.
- [x] **Fix: lo swing warpa la griglia che si sta suonando**, non sempre il quarto. `GrooveEngine::humanDelay` + `swungWithinSpan`: span = il quarto su una parte a 1/8, l'ottavo su una parte a 1/16. Stesso warp, un livello sotto. A 1/16 e swing pieno i sedicesimi cadono su 0, 1/3, **1/2**, 5/6 invece di 0, 1/3, 2/3, 5/6. Il comportamento a 1/8 è **identico a prima**.
- [x] Test riscritto: `VPTests --swing` ora copre quattro casi (1/16 e 1/8, dritto e swing) e ha un'asserzione dedicata — **su una parte a 1/16 l'"&" non si sposta di un campione**, che è tutta la differenza fra shuffle e sincope. `3 passed, 0 failed`.
- [x] Rese verificate col misuratore, non a occhio: `--sub 16 --swing 1` mette l'ottavo a **0,5002** di quarto (dritto) e fitta il modello a sedicesimi con 3,1 ms d'errore.
- [x] **«Acceso» vale 0,65, scelto d'orecchio sui tre render — non la terzina piena.** `kSwingOnAmount` in `MainComponent::applySwing`. Posizione scritta 60,8 %, resa misurata **63,0 %** contro il **61,6 %** del riferimento: 1,4 punti, dentro gli 8 punti di dispersione del riferimento stesso (57,4 vs 65,7 fra i suoi due ottavi). La terzina piena rende 68,9 %, sopra tutto l'intervallo del riferimento. Inseguire il 61,6 % al decimale sarebbe falsa precisione: le due metà della battuta del riferimento non sono d'accordo fra loro.
- [ ] **Ascolto** del tasto in sé: se ON/OFF è la scelta giusta e se a 1/8 lo shuffle suona ancora come deve.

---

### 8. STOP: uscita a trillo dello shaker (poi fade)

Oggi STOP è **immediato**: `engine.stop()` → tracker off, `percussion.silence()`, loop cut (`LoopPlayer`: *no fade, no bar line*). Troppo secco.

Quando si preme **STOP**, lo **shaker** fa la **classica uscita**: un trillo, suono **velocissimo** (colpi molto più fitti della griglia 1/16), **lungo**, con un **fadeout** che lo accompagna fino al silenzio. Non un click che sparisce; è il gesto da percussionista a fine brano.

Solo lo shaker. Congas / clap / cembalo si fermano subito (o in pochi ms, niente click). Il tracker può già essere stopped: il trillo **non** segue più il BPM del brano, è una coda autonoma. Niente alloc/lock/I/O in audio thread.

Durata e densità da fissare all'ascolto (ordine di grandezza: circa 1–2 s di trillo che cala). Se si ripreme START a metà coda, la coda si taglia e si rientra puliti.

- [ ] Stato "outro" in `PercussionEngine` (o equivalente): STOP arma la coda, non `silence()` nello stesso sample.
- [ ] Trillo shaker (down/up rapidissimi) + gain che scende fino a zero; poi silenzio vero.
- [ ] Altre voci: cut immediato, niente trillo.
- [ ] START / TAP a coda aperta: abort della coda, niente doppio attacco.
- [ ] Test: STOP non è più un mute al sample 0; la coda finisce; START durante la coda riparte. Ascolto obbligatorio.
- [ ] Docs skill percussioni: STOP ≠ cut, è l'uscita a trillo.

---

### 9. Volumi separati shaker e congas ✅ (2026-09-03)

Oggi c'è **una knob** per le percussioni. Servono **due volumi** (shaker / congas), indipendenti.

`EngineSettings::instrumentMix` (balance 0..1) e `percussionVolume` sono stati
sostituiti da `shakerVolume` / `congaVolume` (`atomic<float>`, default **1.0**
ciascuno — stesso suono di prima, che aveva percussionVolume fisso a 1.0 e
mix a 0.5). `PercussionEngine::setShakerVolume` / `setCongaVolume` scrivono i
due gain per voce in `render()`, sostituendo la curva di balance. Stesso
schema in `HybridPercussionRenderer::Input` per il path loop registrati
(`shakerVolume` / `congaVolume` invece di `instrumentMix`), così i volumi
valgono anche quando suona la registrazione, non solo il motore sintetico.

UI: la knob unica SHAKER↔CONGAS in FEEL è diventata due manopole indipendenti
("SHAKER", "CONGAS", 0–100%, default 100%). Prefs: chiavi `shakerVolume` /
`congaVolume` al posto di `instrumentMix`.

- [x] Settings + UI (due controlli; default musicali: entrambi al 100%, invariato rispetto a prima).
- [x] DSP: guadagni per voce in render, audio thread safe (solo store/load atomici, nessun alloc/lock).
- [x] Test: uno a zero silenzia solo quella famiglia; l'altro resta (`Tests/TestAiBeat.cpp`, blocco "voice-volume").
- [x] CLAP e CEMBALO (item 10) avranno ciascuno un volume proprio: non unire tutto in una knob — soddisfatto per costruzione, `shakerVolume`/`congaVolume` sono due `atomic<float>` indipendenti in `EngineSettings`, non una curva di balance; item 10 aggiungerà `clapVolume`/`cembaloVolume` con lo stesso schema, non toccherà questi due.

---

### 10. CLAP e CEMBALO oltre shaker e congas ✅ (2026-09-07)

Due voci in più, ognuna con **enable** e **volume proprio** (vedi item 9). Stesso clock a 16th.

**CEMBALO** = **tamburello** (2026-09-03: chiarito dall'utente — non un piatto). Suona con le prese VCSL *Tambourine*: colpo sul pulse, shake sul ritorno. Non è un pattern a parte: è **lo stesso mestiere dello shaker**, con un altro suono. Stessa griglia, stesso thinning (AUTO / 1/4 / 1/8 / 1/16, item 6), stesso swing, stessi accenti/pesi per stile, stesso humanize/dinamica. Acceso, suona sugli stessi step dello shaker; spento, tace. Shaker e cembalo possono stare entrambi on (due timbri sulla stessa parte) o uno solo. L'invariante "niente conga sul uno" **non** riguarda il cembalo: come lo shaker, può (e deve) cadere sul pulse.

**CLAP.** Non segue lo shaker. Suona **solo sul rullo**: in 4/4 i quarti **2 e 4** (backbeat), non ogni step.

**Allineamento (obbligatorio, item 2):** quei quarti devono essere l'1/2/3/4 **di quello che l'app sta sentendo**, non del conto interno se è ruotato. Se `beatInBar` è sfasato di due quarti, il clap "sull'1 e sul 3" dell'app in sala sta sul **2 e sul 4** (e il clap "sul rullo" sta sul 1 e sul 3). Non accettabile. Il clap **non parte** finché la battuta non è fidata (stesso criterio dell'item 2: 1 tenuto, non 1 vs 3 ambiguo, riallineato dopo un taglio di due quarti). Non "sistemare" il pattern a orecchio (mettere 2 e 4 per compensare un 1 sbagliato). Con lucchetto acceso vale il conto dell'utente.

Fatto: due nuovi `Stroke` (`clap`, `cembaloDown`/`cembaloUp`), sintesi fallback dedicata (`PercussionEngine::synthesizeCymbal` per il cembalo, un caso in `specFor` per il clap — nessun sample in `Assets/Percussion/`, non richiesto dall'item). `GrooveEngine::eventsAt` genera il cembalo riusando `spec.shaker[step]`/thinning/accento identici allo shaker (blocco separato, switch proprio); il clap è un pattern fisso (step 4/12) fuori da ogni tabella di stile, gated da `setBarTrusted`. `EngineSettings::clapEnabled/cembaloEnabled/clapVolume/cembaloVolume`, UI in STRUMENTI (tasti CEMBALO/CLAP) e FEEL (manopole), entrambi **off di default**. `kMaxEvents` 4→6 per il posto in più sulla stessa sedicesima.

**Allineamento chiuso con item 2:** il gate del clap legge `BeatTracker::barTrusted` (lucchetto, oppure istogramma sullo zero fuori dalla finestra di rientro). Il proxy a due battute da `barRotations` è stato sostituito; taglio e seek sono coperti da `VPTests --bar`.

**Buco noto (loop registrati, standby):** clap e cembalo esistono solo nel motore sintetico — nessuno stem registrato. Con `VP_ENABLE_RECORDED_LOOPS` on (default) e una registrazione che prende il sopravvento, le due voci si spengono insieme al resto dei "single strokes" (vedi `HybridPercussionRenderer::Input`, commento vicino a `shakerVolume`). Fuori scope qui, è lavoro Standby B.

- [x] Articolazioni in banca (o sintesi fallback) + `Stroke` per clap e per cembalo — **sample registrati VCSL** (CC0): clap dalle prese d'insieme *Claps*, cembalo (= tamburello) da *Tambourine 1/2*. 13/13 articolazioni suonano da registrazione, nessuna sintesi (la sintesi resta solo come fallback senza asset). Scelte per attacco misurato, non a occhio: vedi `Assets/Percussion/ATTRIBUTION.md`.
- [x] Enable + volume per ciascuno.
- [x] Cembalo: riusa la logica/eventi shaker (stessi step, stessa suddivisione); solo sample/voce diversi.
- [x] Clap: solo step del rullo (default 2 e 4); silenzio altrove.
- [x] Clap gated sulla battuta fidata — `tr.barTrusted` dal tracker (item 2).
- [x] Test: `Tests/TestAiBeat.cpp` — cembalo segue 1/4 vs 1/16 come lo shaker (bit-identico allo shaker sui tre subdivision); clap solo su step 4/12 in ogni stile, muto se non fidato; volumi indipendenti (blocco "clap-cembalo" dopo "voice-volume", stesso schema dell'item 9). "non del bar index storto": non testabile fino a item 2 (non c'è ancora un caso reale di rotazione da riprodurre).
- [x] Ascolto render sintetico (2026-09-04): `VPRender --style rock --bpm 120 --bars 8 --click --no-shaker --no-congas --clap --cembalo` → `/tmp/clap-cembalo.wav`. Orecchio: clap sul 2 e 4 del click, cembalo da tamburello sulla griglia shaker. Il clock qui è forzato, quindi **non** copre l'allineamento al brano.
- [x] Ascolto in-app su brano caricato (2026-09-04): clap coincidente col rullo vero, cembalo al posto giusto. Mixer non provato in questa sessione.
- [x] Dopo un taglio di due quarti tace durante la finestra non fidata e torna sul rullo dopo il riallineamento (`VPTests --bar`, item 2).
- [x] Docs in `.claude/skills/percussion-patterns/SKILL.md` (nuovo §7, tabella stroke aggiornata).

---

### 11. Shaker più naturale (non griglia fissa) 🟡 (2026-09-04, codice + test ok — resta ascolto)

Oltre a 1/4, 1/8, 1/16 fissi: in **ottavi**, ogni tanto qualche **sedicesimo** (e analoghi sulle altre griglie, se ha senso). Thinning che **aggiunge** eccezioni, non solo toglie.

- [x] Spec musicale: probabilità, su quali step (e/a), mai sul 1 delle congas; quanto spesso a 1/8 vs 1/4.
  - 1/8: step dispari (e/a), chance `kNaturalEighthChance = 0.22` × `(0.4 + 0.6 * intensity) * dynamics`. Usa i valori già scritti in `spec.shaker[16]`.
  - 1/4: solo gli off-eighth (step 2/6/10/14), chance `0.30` con la stessa scala. Non salta di due griglie (niente 16th su un 1/4).
  - 1/16: niente da aggiungere, invariato.
  - Congas intatte, incluso il guard `step != 0`.
- [x] Implementare in `GrooveEngine` (RNG deterministico, seed come il resto). `soundingShaker` è una decisione sola, condivisa da shaker e cembalo.
- [x] Opzione dedicata (non rubare il tasto 1/8). **NATURALE** in STRUMENTI, default **off**, prefs `shakerNatural`.
- [x] Test: 1/8 puro vs naturale (ci sono odd stroke; non diventa un 1/16 pieno). Stesso per 1/4; cembalo bit-identico sugli extra; default off. In `Tests/TestAiBeat.cpp` dopo il thinning shaker.
- [ ] Ascolto.

**Review (Claude, 2026-09-04)** — comportamento verificato indipendentemente con
una probe che linka solo `GrooveEngine.cpp` (niente suite): 1/8 → 55 ornamenti su
32 battute (limite del test 128), 1/4 → 41 off-eighth e **0** sedicesimi, 1/16 →
stream identico bit a bit, golden 1/8 pre-feature identico, dinamiche 1.0/0.5/0.2
→ 55/35/8 ornamenti. Shaker e cembalo accesi **insieme**: 325 step coincidenti,
0 disallineati. Peggior caso di eventi su una sedicesima, tutte le voci e tutti
gli stili: 4 di `kMaxEvents` 6. Due buchi nei test, il codice è giusto ma il test
non lo dimostra:

- [x] «cembalo NATURALE lands on the same extra steps as the shaker» confrontava
  ```
  due run separate con l'altra voce spenta, e solo i conteggi — la regressione
  che teme (due rollate → desync) si vede solo con entrambe accese.
  **Corretto:** aggiunto `"shaker and cembalo share one NATURALE roll when
  both are on"`, che gira con shaker *e* cembalo accesi e confronta il conteggio
  per singolo step, contando gli ornamenti perché non possa passare a vuoto
  (misurato: 69 ornamenti, 0 step disallineati).
  ```
- [x] «NATURALE never puts a conga on the first quarter's down-stroke» era vacuo
  ```
  due volte: shaker e cembalo erano off (quindi `shakerOn || cembaloOn` false e
  `soundingShaker` mai chiamato) e il loop chiedeva solo lo step 0, che NATURALE
  non tocca mai. **Corretto:** shaker acceso, tutti e 16 gli step percorsi, e
  `naturalOrnaments > 0` nell'assert perché non possa tornare vacuo
  (misurato: 51 ornamenti).
  ```
- [ ] Nota misurata, non un bug: con NATURALE on lo stream RNG condiviso si
  ```
  sposta, quindi le congas suonano **le stesse note** ma con velocity/delay
  humanize diversi (406 eventi su 408 a humanize 0.35). Nessuna nota cambia
  perché ghost e step dispari sono comunque scartati su 1/8 e 1/4. È
  l'invariante che il discard-buffer esiste apposta per proteggere, e non
  c'è un test che la fissi in questa direzione.
  ```

**Igiene dell'albero (Claude, 2026-09-04, fuori dai tre item).** Sette file erano
stati riscritti da CRLF a LF dalla sessione precedente (`MainComponent.cpp`/`.h`,
`Types.h`, `GrooveEngine.h`, `PercussionEngine.cpp`, `BeatTracker.cpp`,
`ATTRIBUTION.md`): il diff leggeva 11k righe invece delle 928 reali e il blame
sarebbe sparito nel commit. Riportati a CRLF, contenuto invariato. E i due mirror
in `.agents/skills/` erano indietro — quello di `percussion-patterns` citava
ancora `setShakerSubdivision`, che non esiste più da tempo — risincronizzati da
`.claude/skills/`. Ricompilati dopo: `VPTests`, `VPRender` e l'host Release
linkano puliti.

---

### 12. PARTE: dropdown stili (fino a DUE-UNO) + DINAMICA accanto 🟡 (2026-09-04, codice ok — resta touch iPad)

Oggi PARTE è una **riga di quadrati**: AUTO, MARCHA, ROCK, DANCE, POP, SAMBA, FUNK, REGGAE, BOSSA, DUE-UNO, e **DINAMICA** in fondo (`placeSquareRow` in `MainComponent.cpp`). Troppi tasti.

**Layout:** un **select custom** (dropdown, non ComboBox nativo iOS/macOS che rompe il look) con tutti gli stili **fino a DUE-UNO**, e **accanto** il pulsante **DINAMICA così com'è** (toggle `dynamicsFollow`, stesso comportamento). Niente altri controlli in quella riga.

Voci del select, in quest'ordine: **AUTO**, MARCHA, ROCK, DANCE, POP, SAMBA, FUNK, REGGAE, BOSSA, DUE-UNO. Default del motore resta **MARCHA / `grooveAuto` off** — è quello che l'app già spediva (`EngineSettings`, test "the default is an eighth-note marcha"); il detector è misurato a 3/9 e AUTO acceso di default lo renderebbe il suono di un install fresco. AUTO è la prima voce del menu. Scegliere uno stile spegne AUTO (`applyStyle`). Rimettere AUTO riaccende il detector. DUE-UNO resta solo manuale (il detector non lo sceglie mai). Non confondere con AUTO in STRUMENTI (item 6).

**Stile e altezza:** select chiuso e tasto DINAMICA **stessa altezza**, stesso chrome (fill, bordo, tipo, colore acceso/spento come gli altri `TextButton` di PARTE/STRUMENTI). Il menu aperto stesso linguaggio visivo, non un picker di sistema. Con AUTO acceso, il chiuso mostra **AUTO** e accenna lo stile rilevato (stesso tint di prima).

- [x] Sostituire i dieci `style*` button con un controllo custom; tenere `dynamicsButton`. `StyleSelect` + `StyleMenuOverlay` in `MainComponent`.
- [x] Prefs: stessa semantica `grooveAuto` / `grooveStyle`. Default motore invariato (MARCHA, AUTO off) — vedi nota sopra.
- [x] Altezze allineate + stesso stile (fill/bordo/underline fuchsia). Da verificare su iPad; compilato Designed for iPad.
- [x] DSP invariato: `applyStyle` / `applyStyleAuto` come prima, commit al prossimo quarto (item 5 resta un bug a parte).
- [x] Touch: il chiuso prende il resto della riga PARTE; il menu si apre sotto e un tap fuori lo chiude. Non copre START/STOP in modo permanente (PARTE sta sotto TRASPORTO, il menu va in giù).

**Review (Claude, 2026-09-04)** — compila (host Release, zero errori). Il default
MARCHA / AUTO off è la lettura giusta del TODO, non un'interpretazione: la nota
sopra la scrive già. Restano due cose:

- [x] `styleMenuLabel` duplicava `vp::toString (GrooveStyle)`, con il `10`
  ```
  scritto a mano in tre punti: l'ordine coincideva, ma se l'enum cambiava le
  etichette mentivano in silenzio. **Corretto (2026-09-04):** la voce 0 è
  AUTO, le altre leggono `vp::toString (GrooveStyle)`, e il conteggio è
  `StyleMenuOverlay::kCount = 1 + (int) GrooveStyle::count` — usato per la
  dimensione di `items[]`, il ciclo del costruttore e il layout in
  `resized()`. Uno stile nuovo nell'enum ora compare da solo nel menu con il
  nome giusto, invece di un `"?"` silenzioso.
  ```
- [ ] **Ascolto/touch:** STRUMENTI è passata a **dieci** celle quadrate su una riga (erano nove; il tasto SWING dell'item 7 è la decima, 2026-09-07)
  ```
  (`side = (W - 9*gap) / 10`): «NATURALE» e «SWING» in quello spazio accanto a «1/16».
  Da guardare su iPad vero prima di dire che è a posto. Unico residuo.
  ```

---

### 13. Tasto «L'1 è QUI» / «SPOSTA L'1» ✅ (2026-09-04)

**Comportamento attuale (non un bug di stato):** è **lo stesso pulsante**.  
«SPOSTA L'1» = sbloccato, un click **sposta l'1 di un quarto e blocca**. Accesso «L'1 è QUI» = *il conto è tuo, l'auto non lo tocca*. Anche un TAP che dichiara l'1 accende il lucchetto.

Su mixer/file il lucchetto **impedisce** il riallineamento automatico (item 2). In palco, di default conviene **sbloccato**.

- [x] Decisione UX (2026-09-04): **B** — tap sul tasto acceso sblocca senza nudge.  
  Un secondo tap (ora sbloccato) sposta di un altro quarto e riblocca. Niente press lungo, niente 5-click. Il 5-click leggeva come tasto rimasto acceso.
- [x] Implementare la decisione. `barControlNudgeOnTap` in `Types.h`; handler in `barButton.onClick`.
- [x] Test del lucchetto: `bar-lock` (motore) invariato; il gesto è coperto da `"SPOSTA L'1 nudges and locks; a tap on L'1 e QUI unlocks without nudging"`.
- [x] Copy: sbloccato **SPOSTA L'1**, acceso **L'1 è QUI** (fill fuchsia). Un tap lo spegne — non serve più girare la battuta per uscirne.

**Review (Claude, 2026-09-04)** — verificato che lo sblocco arriva davvero al
tracker: `VirtualPercussionEngine.cpp` riconcilia `cfg.barLocked` leggendo prima
di riscrivere, quindi lo `store (false)` della UI diventa
`tracker.setBarLocked (false)` invece di essere sovrascritto dalla risposta del
tracker. `BeatTracker` non toccato, `holdBarDecision` intatto. Unico rilievo:
`barControlNudgeOnTap` era una negazione avvolta in una funzione in `Types.h`
con un test tautologico sopra — il comportamento vero stava nell'handler, e il
test non lo copriva. **Rimossa (Claude, 2026-09-04):** inlineato `! locked`
nell'handler in `MainComponent.cpp`, e il test riscritto per girare la stessa
sequenza tap-tap-tap su un vero `EngineSettings` (nudge/lock/unlock/nudge di
nuovo), non su un bool nudo. Verificato PASS con una probe indipendente.

---

### 14. Waveform del brano caricato (seek in impostazioni)

Quando un brano è caricato (`internalPlayer`, `trackTransport`), in **impostazioni**, **sotto** i pulsanti CARICA / PLAY (riga INGRESSO: `sourceButton`, `trackLoadButton`, `trackPlayButton`, …) c'è la **classica onda** da inizio a fine.

Trascinare il dito lungo l'onda = anteprima della posizione. **Rilascio** = riproduce **da quel punto** (`trackTransport.setPosition` + play). Non seek continuo mentre si tiene premuto (niente scratch): il salto è al sollevare il dito.

Senza brano: l'onda non c'è (o è vuota/disabilitata). Playhead che segue il brano mentre gira. Stesso look della pagina SETUP (non un widget iOS nativo).

Il salto è un taglio per il tracker: non lasciare le percussioni sul vecchio punto del brano. Rientro come item 2 (finestra coming-in / epoch), senza restartare il clock del tempo. Cambio di **file** (altro brano) è l'item 3, non questo.

- [x] Peaks dell'onda calcolati al load (fuori dall'audio thread); ridisegnare al resize (`buildTrackWaveform`, 1024 colonne).
- [x] Visibile solo con file caricato, sotto CARICA/PLAY; layout SETUP che cresce (non coprire CLOCK/BUFFER).
- [x] Drag + release → seek + play da lì; playhead in play (`TrackWaveform` + `seekInternalTrack`).
- [x] Tracker/perc: dopo il seek, l'1 e la parte si riallineano al nuovo punto. Seek chiama `engine.notifyTrackSeek()` → finestra `notifyBarReentry` (item 2), **senza** bump di `analysisEpoch`. Coperto dal test mirato `VPTests --bar`.
- [x] Touch: un dito, niente zoom obbligatorio; brani lunghi restano una barra sola inizio→fine.

---

### 15. Pulsanti ÷2 e ×2: servono o si tolgono 🟡 (2026-09-04, ripristinati e testati — resta ascolto)

**Decisione (2026-09-03):** rimossi. Con `tempoOctaveAuto` sempre attivo,
`BeatTracker::updateAutoOctave` tiene il livello metrico; i pulsanti forzavano
solo un override manuale che duplicava confusione. Casi ambigui (es. 50 BPM,
item 1) vanno risolti nel tracker, non con un workaround in UI.

> **RIAPERTO (2026-09-04) — la premessa di quella decisione è stata falsificata.**
> "Casi ambigui vanno risolti nel tracker" presupponeva che il tracker *potesse*
> risolverli. Per almeno una classe di materiale non può, e ora è misurato: una
> battuta straight a 50 BPM e una half-time a 100 BPM producono **lo stesso
> identico segnale** (cassa, hat, rullo, hat, stessa spaziatura, stesso basso
> sotto gli stessi slot). Un correttore che aggiusta la prima rompe la seconda —
> A/B fatto, numeri in `docs/HANDOFF_OCTAVE_50BPM.md` e nell'item 1. Quale dei
> due livelli sia "il tempo" è una **convenzione**, non un dato acustico: nessun
> automatismo può deciderlo, per principio, non per taratura insufficiente.
>
> Quindi la domanda del titolo ("servono o si tolgono") va riaperta con
> un'informazione che nel 2026-09-03 non c'era. Non è un rollback automatico: TAP
> già copre il caso e forse basta. Ma la motivazione scritta sopra non regge più
> così com'è, e va sostituita da una decisione presa sapendo questo.
>
> - [x] **Deciso (utente, 2026-09-04): rimetterli.** «Rimetti ÷2/×2 così se
>   servono li uso.»
> - [x] **Ripristinati** — tre strati, perché erano stati tolti da tutti e tre:
>   - `EngineSettings::tempoOctave` / `tempoOctaveAuto` (`Source/Core/Types.h`),
>   che erano spariti dalla struct;
>   - il motore che li inoltra di nuovo al tracker
>   (`VirtualPercussionEngine::processBlock` → `setTempoOctave` /
>   `setTempoOctaveAuto`, e lo snapshot che riporta il valore vero invece di
>   `true` fisso);
>   - la UI: `halveButton` / `doubleButton` ai lati del numero di BPM, handler,
>   `applyTempoOctave` / `applyTempoOctaveAuto` / `refreshOctaveButtons`,
>   layout a tre colonne e prefs (`tempoOctave`, `tempoOctaveAuto`).
>   Comportamento com'era: premere il livello su cui sei già **torna ad AUTO**
>   (stesso idioma del tasto battuta), il pulsante si accende solo se il livello
>   l'hai scelto tu, e la scelta è salvata fra le sessioni.
> - [x] **Test:** "the halve button reaches the reported tempo" / "and the double
>   button does too" in `Tests/TestAiBeat.cpp`, dentro il percorso veloce
>   `VPTests --octave`. Asserisce la catena intera (impostazione → tracker → BPM
>   riportato) perché è esattamente il punto che si era rotto in silenzio: il
>   tracker aveva ancora l'API, il motore aveva smesso di chiamarla, e nessuno se
>   n'era accorto.
> - [x] **Limite trovato provando, da sapere quando li usi:** l'app riporta solo
>   **50–215 BPM**, poco più di due ottave. Quindi **un tasto dei due è spesso
>   inerte**, per costruzione: sopra ~107 BPM il ×2 chiede un numero fuori range
>   e il valore torna dov'era (misurato: da 120, ×2 chiede 240 e viene ripiegato
>   a 120); sotto 100 BPM è il ÷2 a non avere spazio. Entrambi agiscono solo fra
>   100 e 107. **Il caso che ti serve funziona**: il brano a 50 letto come 100
>   scende a 50 con un tocco di ÷2, perché 50 è esattamente il fondo scala.
>   Se in futuro servisse più margine, si allarga `kMinBpm`/`kMaxBpm` in
>   `TempoEstimator.h` — ma è una modifica che tocca tutto il tracking, non una
>   costante da girare.
> - [ ] **Ascolto:** provare i due tasti su un brano lento reale — è la verifica
>   che il TODO chiede per gli item musicali e che nessuno ha ancora fatto.

- [x] Verificare ascoltando: con AUTO ottava, ÷2/×2 cambiano qualcosa di utile o solo confondono.
- [x] Se inutili: togliere i due pulsanti, layout, prefs `tempoOctave` da UI, `applyTempoOctave` / click handler. Lasciare `tempoOctaveAuto` e il path automatico in `BeatTracker` se ancora usati dal decoder.
- [x] Se si toglie anche `setTempoOctave` utente: test esistenti su ottava manuale da aggiornare o cancellare **solo** quelli del override; non allentare i test di lock/BPM. (`setUserOctave` nel decoder resta per i test `octave-control`.)
- [x] Nessuna regressione su START, TAP, suddivisione, «SPOSTA L'1» — gate eseguito due volte: `575 passed, 1 failed` (`self-leak MIX quiet src`, ottava intermittente 81,3/120 BPM), poi `576 passed, 0 failed`. Il secondo run è verde; il caso intermittente resta annotato, non attribuito a questo item.

---

### 16. Guadagno automatico dell'analisi: ora attenua anche vicino al clipping 🟡 (2026-09-04, codice + gate ok — resta ascolto)

Utente: abbassando o alzando il **volume di ingresso** dall'app, a volte l'ascolto (tempo/battito) sembra migliorare. Causa trovata: il guadagno automatico che normalizza il segnale per la rete (`applyAnalysisMakeup`, target picco 0.20, tetto 24x) era **solo boost** — clampato a un minimo di 1.0, non attenuava mai un ingresso già più forte del target.

Tentativo scartato: simmetria piena (attenuare sempre verso 0.20 come si boosta verso 0.20). Rompe `octave-sweep` a 168 BPM in `Tests/TestAiBeat.cpp` — quel banco sta a un picco di ~0.25, già vicino al target, e una minuscola attenuazione (gain 0.8, ~2 dB) lo fa leggere a metà tempo. 168 BPM è proprio nella zona già documentata nel commento sopra `kMakeupTargetPeak` come sensibile ("letture a metà tempo sopra i 150 BPM"). Sistemare per davvero servirebbe la stessa validazione estesa (tanti brani/stili) usata per tarare 0.20, fuori scope qui.

Fix effettivo (più stretto): attenua **solo** sopra un picco di **0.90** (vicino al vero clipping, che nessun guadagno a valle può disfare), verso 0.90. Tra 0.20 e 0.90 il guadagno resta esattamente 1.0, invariato — tutto l'intervallo già misurato nella tabella del commento (0.04–0.60) resta intoccato.

- [x] Trovata la causa (clamp min a 1.0 in `applyAnalysisMakeup`, `Source/Audio/VirtualPercussionEngine.cpp`).
- [x] Fix stretto: `kMakeupClipGuardPeak = 0.90f`, attenua solo sopra, invariato sotto (righe ~37-66 e ~853-860 di `VirtualPercussionEngine.cpp`).
- [x] Estratto `octave-sweep` in `vpRunOctaveSweepTest` (`Tests/TestAiBeat.cpp` + `TestAiBeat.h`), richiamabile da solo con `VPTests --octave` (~3-4 min invece dei ~5+ min della suite intera) — usare questo per iterare su qualsiasi cosa tocchi il livello di analisi, non la suite intera.
- [x] Verificato con `VPTests --octave`: 168 BPM torna a leggere giusto (gain=1.000, "on it").
- [x] **Gate fatto (2026-09-04):** `VPTests` intera su richiesta dell'utente — **610 passed, 7 failed**, e le 7 sono tutte preesistenti e attese: 2 leak (`no block of it is moved…`, `a leak that does not land on a whole sample…`) e i 5 RED a 50 BPM dell'item 1 (`a 50 BPM bar makes its 100 BPM hi-hat…`, `mixer/file slow kit holds 50 BPM…`, `mixer/file audible 50 BPM quarter…`), lasciati rossi apposta. **Nessuna regressione nuova**: niente dipendeva dal vecchio clamp boost-only.
- [x] **Il trim INPUT non riavvia più l'analisi (2026-09-07).** Le decisioni sul
  livello fisico (`sourceAudible`, dinamica, rientro e `analysisEpoch`) usano il
  residuo dopo canceller diviso per il trim; il make-up continua correttamente a
  vedere il livello realmente consegnato a BeatNet. `VPOps --input-gain`: zero
  epoch aggiuntive, delta +0,07 ms sul percorso iPad e 0,00 ms diretto. Anche
  `--voice-toggle`: zero epoch, +1,80 ms iPad / 0,00 ms diretto.
- [ ] Ascolto: non fatto. Verificare con un ingresso reale volutamente troppo "caldo" (mixer a livello di linea o trim alto) che il sintomo originale dell'utente (regolare il volume per sentire meglio) sia davvero sparito.
- [ ] Se in futuro si vuole la simmetria piena (attenuare sempre verso 0.20, non solo sopra 0.90), serve rifare la validazione a più brani/stili come quella già in commento sopra `kMakeupTargetPeak` — non è un fix da una riga.

---

### 17. L'ottava non cambia più sotto le mani di chi suona 🟡 (2026-09-04, codice + gate ok — resta ascolto)

Segnalato dall'utente: *«se c'è in esecuzione il percussionista e di punto in bianco
dimezza o raddoppia, si incasina tutto»*. Confermato con misura.

**Cosa succedeva.** `BeatTracker::updateAutoOctave` poteva spostare il livello
metrico in qualsiasi momento, anche a brano avviato e parte suonante. Il caso che
lo mostra: una band che suona **a 168 BPM** sta esattamente sul confine
`kOctaveTooFast = 168`, e la normale deriva umana glielo fa attraversare. Traccia:

```
t= 20.3  bpm= 167.49     ← il tempo deriva verso l'alto
t= 23.5  bpm= 168.20     ← supera la soglia
t= 26.7  bpm=  84.24     ← dimezza, a metà brano
t= 58.7  bpm=  83.46     ← e non torna più: per risalire servirebbe
                            scendere sotto kOctaveTooSlow = 49
```

Riproducibile a due decimali su esecuzioni ripetute. In app: BPM mostrato che si
dimezza, densità della parte che cambia, e il percussionista che si ritrova la
griglia spostata sotto le mani.

**Cosa ho cambiato.** Non la soglia e non la politica di quale livello sia giusto:
solo **quando** è lecito cambiarlo. L'ottava automatica ora si muove soltanto se
**non sta suonando nulla** — prima che la parte entri, a uno stand-down, dopo STOP
— e resta ferma per tutta l'esecuzione. Usa `sounding`, lo stesso segnale che il
codice già adopera per non ruotare la battuta sotto chi ascolta.

Tre cose restano aperte apposta:

- **un brano nuovo riparte pulito** (`setInputEpoch` azzera comunque il livello):
non resti incastrato sul livello del pezzo precedente;
- **÷2/×2 funzionano anche mentre suoni** (strada manuale, non automatica): se il
livello congelato è sbagliato, il rimedio è un tocco — è il caso per cui item 15
li ha rimessi;
- **la scelta iniziale resta automatica**, in acquisizione.

**Misure, mixer, materiale con deriva 3 BPM:**


| caso        | prima                                      | dopo                                                                                                        |
| ----------- | ------------------------------------------ | ----------------------------------------------------------------------------------------------------------- |
| **168**     | 83.68, oscillazione **84.98**, mai stabile | **166.90**, oscillazione **2.02**, stabile in 3.6 s                                                         |
| 158         | 157.57                                     | 157.01 — invariato                                                                                          |
| 176         | 88.16                                      | 88.12 — dimezza ancora, ma **in acquisizione** (2 s) e poi fermo: corretto, 176 sta davvero sopra il limite |
| 132         | 132.36                                     | 132.39 — invariato                                                                                          |
| 104         | 103.53                                     | 103.67 — invariato                                                                                          |
| 200 (fisso) | 100.25, 3 salti, 1 battuta rotta           | 100.02, **0 salti**, oscillazione da 2.10 a 0.13                                                            |


- [x] Modifica in `BeatTracker::updateAutoOctave` (`Source/Tracking/BeatTracker.cpp`), con il commento che riporta la traccia sopra.
- [x] Verificato con `VPProbe --trace --live --mixer plain <bpm>` sulla matrice qui sopra. **Comando di regressione:** `VPProbe --trace --live --mixer plain 168` deve finire vicino a 167, non a 84.
- [ ] **Ascolto:** suonare un pezzo a ~168 che deriva e verificare che la parte non cambi densità a metà. Non fatto.
- [x] Gate `VPTests` intera **fatto (2026-09-04)**: 610 passed, 7 failed, tutte preesistenti (2 leak + i 5 RED a 50 BPM dell'item 1). Il congelamento dell'ottava sotto `sounding` non rompe nulla nella suite.

---

### 18. Assestamento lento e imprevedibile 🟡 (2026-09-04, misurato e circoscritto — resta il confine a 170)

Trovato durante l'indagine dell'item 17, **non risolto e deliberatamente non
toccato**. Con lo stesso identico materiale, la stessa corsa dà esiti diversi:


| tempo (fisso) | corse | tempo per assestarsi                                                              |
| ------------- | ----- | --------------------------------------------------------------------------------- |
| 104           | 4     | 2.3 s, 1.7 s, **31.7 s**, e una che non ci arriva mai (oscillazione 9.7, 5 salti) |
| 108           | 1     | **29.0 s**                                                                        |
| 100           | 4     | 1.9, 2.0, 2.1 s … e una da **27.0 s**                                             |


Non è un tempo che rompe il tracker: è variabilità **fra esecuzioni**. La skill del
tempo la descrive già (il worker della rete non pubblica lo stesso numero di
ipotesi a ogni corsa, dipende dallo scheduler; esiste `--sync` proprio per
neutralizzarla nelle misure). Quindi **una parte potrebbe essere artefatto del
banco** — ma trenta secondi per agganciare un tempo comune, se capita sul palco,
si sente.

- [x] **Risposto (Claude, 2026-09-04): non è (solo) artefatto del banco.**
  `scripts/probe_steady_tempo.cpp` gira il decoder **senza rete e senza
  scheduler** — deterministico dato il seme — su tempo costante con deriva 3 BPM
  e jitter 10 ms (le impostazioni `--live`), 300 s, 10 semi per tempo. La
  variabilità resta. Quindi non serve più `--sync` per decidere: il fenomeno è
  nel decoder, non nel worker.

  | BPM                         | corse con uscite (su 10) | errore peggiore | fuori più a lungo |
  | --------------------------- | ------------------------ | --------------- | ----------------- |
  | 60                          | **10**                   | **109%**        | **288 s**         |
  | 75 / 90 / 100 / 110         | 0                        | —               | —                 |
  | 120 / 132 / 140 / 150 / 160 | 1–3                      | 4–5,5%          | 2–3 s             |
  | 170                         | **4**                    | **50%**         | **26,5 s**        |

  Letto per bene, non è "il tracker è instabile":
  - **75–160 è sano.** I blip da 2–3 s al 4–5% sono transitori di acquisizione,
  e il tracciamento della deriva è dentro 1–2 BPM (verificato a 132).
  - **I guasti stanno ai bordi della gamma, e sono metà/doppio, non il tempo.**
  **Attribuzione corretta (stessa giornata, dopo una seconda misura).** In una
  prima lettura avevo dato la colpa a `kOctaveTooFast = 168`: **sbagliato**, quella
  costante sta solo in `BeatTracker.cpp` e questa probe è a livello di decoder, che
  non la vede nemmeno. Il fenomeno è il **livello metrico scelto in acquisizione**.
  Misurato con 20 semi per tempo (rapporto riportato/vero alla prima risposta):

  | BPM          | giusto    | a metà   | al doppio     |
  | ------------ | --------- | -------- | ------------- |
  | 50 / 60      | 2/20      | —        | **18/20**     |
  | 70           | 9/20      | —        | 11/20         |
  | **80 → 160** | **20/20** | 0        | 0             |
  | 170 / 180    | 17/20     | 0        | 0 (3 "altro") |
  | 200          | 13/20     | **7/20** | 0             |

  Cioè: **la gamma dove vive quasi tutta la musica (80–160) è già pulita**, 20 su
  20. Quello che resta è raddoppio sotto i 70 e dimezzamento a 200 — esattamente
  l'ambiguità metà/doppio che l'item 1 ha dimostrato **indecidibile dall'audio**,
  non un bug da riparare. Il rimedio previsto è già in campo: TAP e ÷2/×2.
- [x] **Non inseguire questa con modifiche al tracker.** La prima ipotesi
  (isteresi sul confine dell'ottava) è stata scartata: il confine non è nel
  decoder e la gamma centrale non ha il problema. Tarare qui vorrebbe dire tarare
  sul rumore. Il lavoro che paga è l'**item 19**, che è un bug vero e riparabile.
- [x] **CORRETTO il 2026-09-07: «i blip da 2–3 s a 120–160 sono transitori di
  acquisizione» era sbagliato, ed era un bug vero.** La probe ha sempre avuto un
  flag `verbose` che stampa *quando* avviene l'uscita, e su questa gamma non era
  mai stato usato. Le uscite cadono a **80, 88, 155, 171, 243, 270, 282 e 295
  secondi**: in mezzo alla corsa, non in acquisizione. Causa trovata e corretta —
  vedi **item 21**. Quello che resta di vero in questo item è la parte sui
  **bordi** (raddoppio sotto i 70, dimezzamento a 200): quella è davvero
  l'ambiguità metà/doppio dell'item 1 e non si tocca.

---

### 20. Le congas «escono» dove lo shaker tiene 🟡 (2026-09-07, misurato e corretto sul canceller — resta ascolto)

Segnalato dall'utente: *«se metto lo shaker resta abbastanza allineato, ma se
inserisco le congas tende a uscire e ad avere più difficoltà»*.

**Riprodotto e misurato.** Il canceller del rientro (`subtractSpeakerLeak`)
tagliava il nostro output in **due** bande a ~1,5 kHz, e le due voci stanno
esattamente ai due lati della linea: misurato su otto battute di marcha a
ottavi, lo shaker ha **9,5 dB sopra** la linea, le congas **13,6 dB sotto**. Le
congas ricevevano quindi **un solo guadagno per tutto 0–1,5 kHz**, che è proprio
la gamma che un altoparlante piccolo rimodella di più: passa il corpo e butta
via la fondamentale. Un guadagno solo non può dire «niente di questo e tutto di
quello», quindi trova il compromesso — sottrae troppo sulla fondamentale e
troppo poco sul corpo. Numeri, banco `VPTests --leak` righe `leak-voice`
(fixture stanza a una parete, funk a ottavi, 8 s, quota del nostro rientro
rimossa):

| voce | prima | ora |
|---|---|---|
| solo shaker | 30,4 % rimossa, blocco peggiore 0,88 | 33,8 %, 0,90 |
| **solo congas** | **6,0 % rimossa, blocco peggiore 1,84** | **13,4 %, 1,03** |
| shaker + congas | 14,7 %, 1,06 | 15,9 %, 1,06 |

Il numero che conta è il blocco peggiore delle congas: **1,84** vuol dire che
su quel blocco la sottrazione metteva nell'analisi **più** di quanto togliesse —
il canceller stava aggiungendo un transiente sulla griglia del clock, cioè
esattamente il segnale che conferma al tracker qualunque cosa stia già credendo.

- [x] **Fatto: terza banda.** `kLeakBands = 3`, split a ~250 Hz e ~1,4 kHz (il
  secondo è il vecchio 0.18, lasciato dov'era: la banda dello shaker non
  cambia). Minimi quadrati su tutte e tre insieme via Cholesky con ridge
  **relativo** alla traccia (`kLeakRidge = 1e-2`), non con un fondo assoluto:
  la banda di mezzo, presa come differenza di due poli singoli, ha molta meno
  energia delle due ai lati ed è quella che un feed **senza** rientro riesce a
  spingere. Su mixer, dove il ritorno porta anche il basso, le due bande basse
  si adagiano sullo stesso guadagno e non cambia niente: il fit non ha bisogno
  che gli si dica su che path è.
- [x] **Costo del ridge, dichiarato:** le 54 righe a copia esatta passano da
  0,0000 a **0,0179** (limite 0,10 — un ordine di grandezza di margine invece di
  tre). In cambio il banco no-leak sul path speaker migliora: blocco udibile
  peggiore 1,092 contro 1,111 senza ridge e 1,061 a due bande, e la riga a 128
  frame passa da **11,40 %** (che sforava il proprio limite del 10 %) a 5,13 %.
  Netto: `VPTests --leak` va da 2 fallimenti a 1.
- [x] Banco nuovo, righe `leak-voice`: `roomLeakRun` prende `Voices`
  (shaker / congas / entrambe). Una media sulle due metà non poteva rispondere
  a una segnalazione che parla di una metà sola.
- [x] `VPOps --voices` (ex `--silentpart`, ora quattro passate: muta, solo
  shaker, solo congas, entrambe) sul path iPad a 118 BPM: 6,85 / 0,91 / 1,47 /
  4,96 ms di errore di fase. **Da leggere con cautela**, e la testa di
  `probe_ops.cpp` dice perché: passate indipendenti della rete vera non
  committano lo stesso BPM all'ultimo decimale, e qui la muta esce *peggio* di
  entrambe le voci singole — il rumore fra passate (~6 ms) copre l'effetto. Il
  banco deterministico (`leak-voice`) è quello su cui è stata presa la
  decisione.
- [ ] **Ascolto.** Nessun orecchio umano su questa correzione. Serve rimettere
  le congas su mixer / brano caricato e dire se il problema che si sente è
  ancora lì.
- [ ] **Seconda ipotesi, non ancora misurata, e probabilmente quella che si
  sente su brano caricato.** Con `followSource = internalPlayer` il rientro
  **non esiste** (`process` salta `subtractSpeakerLeak` su `directFile`): lì le
  congas non possono disturbare il tracker, quindi se «escono» anche su file il
  colpevole è un altro, ed è l'**item 2**. Lo shaker è invariante alla
  rotazione della battuta — un pulse è un pulse ovunque cada l'1 — mentre la
  figura delle congas, la frase di otto battute e il fill *dipendono* da quale
  quarto è l'uno. Un 1-vs-3 sbagliato, o una rotazione automatica sotto le mani,
  si sente **solo sulle congas**. Chiedere all'utente su quale fonte l'ha
  sentito prima di lavorarci.
- [x] **Fallimento preesistente risolto (2026-09-07): la ricerca del ritardo
  molla un aggancio buono su un blocco muto.** Isolato con un worktree su HEAD
  pulito (0,2794 anche lì: non era né la terza banda né la sessione parallela) e
  con un probe dedicato che stampa `leakDelaySamples` blocco per blocco. La
  ricerca si rifà da zero ogni quattro blocchi e decide sulla correlazione **di
  quel solo blocco**, mentre il fit del guadagno lì accanto accumula mezzo
  secondo prima di credere a qualcosa. Su una parte rada non è simmetrico: fra
  due colpi il punteggio del ritardo giusto crolla nel rumore, e la periodicità
  della parte stessa lascia picchi d'inviluppo casuali ad altri lag lungo una
  finestra di diecimila campioni. Misurato: lasciava un 8417 corretto e agganciato
  per un 4811 che su un blocco muto faceva 0,1971 contro lo 0,0739 del titolare —
  due numeri senza significato, uno solo nominalmente più grande — e poi vagava
  1,2 s (4811, 9613, 7429, 5633, 8746, 7421) buttando gli accumulatori a ogni
  salto, perché `delay != leakFitDelay` scatta a ognuno.
  **Fix:** la soglia 0,12 resta quella che è, la soglia per *trovare* un percorso
  partendo da niente; **abbandonarne** uno già trovato ora richiede un margine
  (`kDelaySwitchMargin = 0.15`), non di essere nominalmente più grandi.
  `leak-room frac+noise`: **0,2794 → 0,0888** (blocco peggiore 0,99 → 0,13),
  cioè di nuovo lo 0,0957 documentato. `VPTests --leak`: **49 passed, 0 failed**.
- [x] **Effetto collaterale, ed è quello che riguarda la segnalazione:** il
  vagabondaggio colpiva soprattutto la voce più rada. Le congas passano da
  13,4 % a **25,2 %** di rientro rimosso, blocco peggiore da 1,03 a 0,99. Quadro
  completo delle tre tappe:

  | voce | 2 bande (origine) | 3 bande | 3 bande + isteresi |
  |---|---|---|---|
  | solo shaker | 30,4 %, peggiore 0,88 | 33,8 %, 0,90 | 33,8 %, 0,90 |
  | **solo congas** | **6,0 %, 1,84** | 13,4 %, 1,03 | **25,2 %, 0,99** |
  | entrambe | 14,7 %, 1,06 | 15,9 %, 1,06 | 15,9 %, 1,06 |
- [ ] **Gate non lanciati:** `VPTests --makeup` (l'altra metà dello stesso
  argomento: il nostro output che muove la nostra analisi) e `VPTests` intera.
  Costano minuti di worker neurale in tempo reale — chiedere prima.

---

### 21. Le percussioni ogni tanto rallentano o accelerano, e ci mettono a rientrare 🟡 (2026-09-07, causa trovata e corretta — resta ascolto)

- [x] 08/09, segnalazione a ~120 BPM: corretto il passaggio dal recupero rapido
  al filtro ordinario, che conservava la vecchia correzione. Sul clock dopo un
  passaggio sfasato: 3.605 → 0.752 s per tornare stabilmente entro 8 ms; con
  scarto più grande 0.880 s. Durata del recupero derivata dalla distanza e dal
  rail invariato del 20%, scarti confermati <0.25 beat. `probe_recovery`: 84 PASS.
- [ ] Nuovo gate `probe_recovery --slow-passages`: 18 FAIL a 52 BPM, residui
  sotto la soglia di conferma e deadband ordinaria da 13.85 ms. I casi a 256
  fallivano già prima. Restano aperti anche l'attribuzione del problema sul
  brano reale a ~120 BPM e la misura degli attacchi audio: non sono questi
  tempi sintetici a provare il rientro dell'app sulla registrazione.

- [x] 08/09, nuova segnalazione: corretto il recupero che non si confermava
  quando arrivavano seriali distinti a ottavi. I colpi dentro 0.55 beat ora
  conservano il primo candidato e la correzione accumulata. `probe_recovery`:
  18 casi nuovi da FAIL a PASS, 12 precedenti e 5 controlli negativi PASS.
  A buffer 256: scarto <8 ms in 1.733/0.901/0.533 s dalla prima osservazione
  a 52/100/168 BPM, stabile oltre due beat. Solo clock con fase nota;
  il ritardo del riconoscimento sul brano reale resta da localizzare.

- [x] 08/09: sorgente armonica collegata a fase, ingresso e clock sul diretto;
  `--harmonic-entry` 4/4. Con accordi radi l'ingresso resta lento (18.79 s),
  non confondere questa integrazione con riconoscimento istantaneo senza batteria.

- [x] 08/09: timer bassa fiducia e filtro indipendenti dal buffer;
  Recupero anche con fiducia alta: due beat concordanti, 12 casi clock + 3
  controlli mirati; verifica audio completa ancora separata nello step 5.
  `VPTests --state-timing` 3/3 PASS (64/256/1024). Ripresa in HANDOFF_TEMPO.md.

Segnalato dall'utente su un **brano registrato dal vivo**: *«ogni tanto le
percussioni tendono a rallentare o a velocizzare e poi ci impiegano molto a
rientrare nel tempo»*.

- [x] **Rientro quando tornano colpi puliti.** Un fill, un cambio di volume o
  **Aggiornamento 08/09:** il fronte ora arma soltanto; due beat freschi e
  concordanti confermano. Peggioramento e cambio di riferimento annullano.
  `probe_recovery`: 6/6 PASS; mezzo beat misurato dalla conferma, solo clock.
  l'attivazione di un'altra percussione abbassano correttamente la fiducia e
  limitano quanto la griglia segue quell'evidenza; quando la fiducia tornava
  alta, però, il residuo passava ancora dal filtro lento di mantenimento. Ora il
  fronte povero→pulito apre per mezzo beat un recupero continuo, senza snap né
  riavvio del clock. `probe_steer --reentry`, da 0,075 beat di scarto: dentro 8
  ms in **0,579 / 0,312 / 0,253 / 0,179 s** a 52 / 96 / 120 / 168 BPM, intervalli
  fra impulsi **0,99–1,14x**, quindi nessun colpo doppio o saltato. Il banco
  pulito 60 s × 8 semi resta invariato.

**Nota di verifica 08/09:** il vecchio `fillRecovery` misura il ritorno della
velocità, non della fase; non esclude uno scarto persistente. Risultati e limiti
aggiornati in `docs/HANDOFF_TEMPO.md`. Step 1–4 salvati; verifica finale parziale
(detector armonico su audio ora misurato ma non superato; manca registrazione live).
`VPTests --harmonic-audio`: dopo aver allineato il warm-up produzione/probe e
rimosso il doppio gate sulla fonte, il caso senza pad entra a 23.371 s, 99.917
BPM, otto attacchi entro 18.17 ms. Batteria sola e accordo fermo si astengono.
Restano due FAIL: ingresso oltre 4.8 s e caso con pad senza fase valida. Serve
una fonte di pulse non percussivo; non è risolto l'ingresso entro due battute.
Dettagli nel passaggio di consegne.

**Verifica worker reale 08/09:** sui soli stems musicali BeatNet passa 100 BPM
senza pad (2.347 s, 101.940 BPM), ma legge 52 come 103.927 e 168 come 91.585;
con pad falliscono tutti i 52/100/168. Fit e copertura sono buoni anche sulle
ottave errate, quindi non sono distinguibili con un'altra soglia di fiducia.
Ridurre HarmonicTempo a due cambi è stato provato e ripristinato: produce 200
col pad e coerenza 1.0 sulla batteria. Serve la registrazione reale e, se
confermata, un modello adatto agli accompagnamenti tonali; niente euristica
onset è stata lasciata attiva. Misure complete in HANDOFF_TEMPO.md.

**Conclusione storica, limitata alla velocità:** `scripts/probe_steer.cpp`
(nuovo, clock da solo, deterministico) dà una fase sbagliata di 0,25 di beat per
**due secondi** e misura quanto ci mette la griglia a rientrare — **0,06–0,28 s**
a tutte e tre le forze di inseguimento, e ALTO è la più veloce. Lo sterzo di fase
non può produrre un rientro lento.

**È il decoder, ed è il rilevatore di cambi di tempo bruschi che scatta sul
jitter.** `probe_steady_tempo --verbose` su tempo costante, impostazioni
`--live`, 300 s × 10 semi: **otto uscite** fra 110 e 160 BPM, tutte **4,0–5,5 %**
per **1,8–3,0 s** — a 120 BPM è la griglia che va a 125 e torna. Tracciato il
momento della conferma: **il comb leggeva il tempo giusto** (121,21 contro 121,48
vero) mentre la transizione pubblicava **126,85**, e i due fit leggevano `0.00`
perché la transizione li aveva appena buttati. Nessuno chiede al comb.

Due difetti, entrambi nei termini del file stesso:

1. **La soglia per aprire un candidato stava sotto il pavimento dichiarato.**
   `needed = max(1 BPM, 3 × jitter)`; col jitter reale (~1,4 %) vale **4,2 %**,
   mentre `kTransitionSmallestStep` dice duecento righe più su che **5 %** è «il
   più piccolo cambio che valga la pena rivendicare». Fra i due c'era una fascia
   di dimensioni su cui il detector agiva e che non era disposto a difendere.
2. **La prova di coerenza era quattro volte più larga di come si legge.**
   `deviation` è **metà** della differenza relativa e la tolleranza era **2 ×
   jitter**: due intervalli potevano discordare di **4 × il jitter** ed essere
   chiamati «un tempo solo». Misurato alle conferme false: coppie come
   **0,4635 s e 0,4826 s — distanti il 4,1 % fra loro, quanto il gradino che
   rivendicavano.** Non è un periodo misurato due volte; sono due numeri diversi
   la cui media capita lontano dal tempo commesso.

- [x] **Fix, due righe in `BeatDecoder::observeTransition`:** `needed` non scende
  mai sotto `kTransitionSmallestStep`; la tolleranza passa da `2 × jitter` a
  `1 × jitter` (i pavimenti assoluti restano dove sono — non erano loro a essere
  larghi). Un gradino vero supera la barra a due volte lo scatter circa cinque
  volte su sei, e se manca ha subito la coppia dopo: il codice ricade su un
  nuovo candidato invece di aspettare un beat.
- [x] **Effetto: 8 uscite → 3.** 110, 120 e 132 BPM ora **puliti su 10 corse da
  300 s ciascuno**. Restano 144 e 160 con una a testa: le loro due coppie
  concordano davvero entro il jitter, e passano per il pavimento assoluto
  (`kTransitionLineCoherence`). Abbassare quello mette a rischio i gradini su
  materiale pulito — non l'ho toccato.
- [x] **Costo: nessuno misurato.** `probe_tempo_step` è **identico riga per riga**
  prima e dopo (A/B contro `git show HEAD:`). `VPAlign`: i cinque gradini protetti
  tutti **PASS**, ±1 BPM in **0,78–1,47 s**, fase **23,3–24,4 ms**, zero violazioni
  di impulsi — gli stessi numeri che la skill documenta; le rampe restano a
  `transizione=0`. `--octave focused` 6/4 con le quattro rosse deliberate
  dell'item 1, `--swing` 3/0, `--leak` 49/0.
- [ ] **Ascolto.** È l'unica verifica che non posso fare io: rimettere lo stesso
  brano live e dire se le escursioni si sentono ancora.
- [ ] **Aperto, misurato ma non toccato: il default spedito è ALTO.** `Types.h` e
  `MainComponent` impostano `FollowStrength::high`, mentre la tabella della skill
  chiama MEDIO il default. Su materiale live simulato ALTO fa escursioni **2–3
  volte più grandi** di BASSO (peggiore 6,7 % contro 2,6 % a 144 BPM; **0,55
  contro 0,06 secondi al minuto** oltre il 2 %) a parità di rms. Su questo banco
  non compra niente — ma il banco non contiene un cambio di tempo vero, che è
  l'unica cosa per cui ALTO esiste, quindi **non ho cambiato il default**. Serve
  o un banco onesto sul recupero, o l'orecchio.
- [ ] **Restano le 3 uscite a 144/160.** Il discriminante che manca è il comb, che
  aveva ragione tutte le volte — ma al momento in cui il candidato si apre il comb
  è ancora sul tempo vecchio anche su un gradino vero, quindi lì non separa. Per
  usarlo servirebbe *confermare e poi ritrattare* se il comb non corrobora entro
  il suo tempo di assestamento: è la stessa forma del watchdog già provato e
  ritirato (vedi il commit 8528d4e), e non va ritentata alla cieca.

---

### 22. Ai tempi lenti «esce e non tiene», l'1 arriva tardi, e con lo swing l'aggancio costa 13 s 🟢 (2026-09-07, corretto — resta ascolto sul brano dell'utente)

Segnalato dall'utente: *«spesso esce e non tiene, brani tipo a 60 o 80 bpm. Quelli
più veloci sembra vadano meglio. In più il primo quarto a volte lo riconosce ma
molto in ritardo.»*

**I due sintomi sono lo stesso sintomo.** Se la griglia è a doppia velocità, la
battuta viene contata su una griglia sbagliata e l'1 non può essere trovato.

**Misurato** (`probe_steady_tempo`, decoder da solo, deterministico): a **60 BPM
10 corse su 10** leggono il doppio e non rientrano mai; a **70 BPM 9 su 10**
raddoppiano e rientrano dopo 0,7–16 s — è il «esce e non tiene»; **80, 90, 100
puliti**. Nota che quella probe genera **un impulso per battito e silenzio in
mezzo**: raddoppiare lì vuol dire mettere metà griglia sul silenzio, quindi **non
è** il caso indecidibile dell'item 1, dove gli ottavi ci sono davvero.

Tracciato: **il comb leggeva 61,29 a salienza 1,00 per tutta la corsa** mentre il
pubblicato era 122,21, e `octaveMismatch` restava **0**. Tre difetti in fila, uno
dentro l'altro:

1. **La valvola di sfogo era bendata.** `combDisagrees` confronta `combBpm`, che è
   `foldToAnchor (tempo.bpm())` — la lettura del comb **già ripiegata sull'ottava
   in uso**. Il test che esiste per accorgersi di un errore d'ottava lo faceva su
   un valore a cui l'ottava era appena stata tolta: `log2(122,2/122,6) = 0,004`
   contro una soglia di 0,25. Nessun disaccordo, mai. È la stessa trappola che
   `checkGridPhase` documenta per la *fase* («una griglia che si àncora sul
   levare non è solo sbagliata, è stabile… e non c'era niente nella catena che
   potesse accorgersene»), risolta lì mettendo il fold **fuori** dal gate; per il
   *ritmo* il fold **era** il gate.
2. **La guardia `unprovenSlowerOctave` usava come prova a favore una cosa che
   dichiara priva di valore.** Il commento sopra dice: *«una griglia doppia può
   sembrare sana perché ogni battito rilevato cade su un tick sì e uno no»* — e
   poi usa `gridHealthy` per vietare la correzione. Il dato che manca era lì
   accanto, gratis: `coverage = keep/n` vede una griglia **troppo lenta** (deve
   buttare metà eventi) ma non una **troppo veloce**, che li tiene tutti. Il tell
   sono gli **indici** su cui cadono: 0, 2, 4, 6 invece di 0, 1, 2, 3.
3. **E anche quando lo scatto partiva, era un no-op.** `bpm = clamp (combBpm…)`
   adottava di nuovo il valore ripiegato: buttava via la storia dei battiti e
   ricommetteva **lo stesso tempo doppio**. E non spostava `anchorBpm`, quindi la
   riacquisizione successiva ripiegava subito indietro.

- [x] **Fatto:** `fitPeriod` restituisce il **salto mediano di indice** dei battiti
  tenuti (1 su una griglia al polso, 2 su una raddoppiata; misurato: 2,0 esatto a
  60 BPM con coverage 1,00 e residuo 0,030). La guardia lo richiede denso prima di
  difendere la griglia. `combDisagrees`, il voto e lo scatto usano il comb
  **grezzo** (`combRawBpm`); lo scatto sposta anche l'ancora. Tutto il resto
  continua a usare il comb ancorato.
- [x] **Effetto, `probe_steady_tempo` 300 s × 10 semi**, contro la tabella
  dell'item 18:

  | BPM | prima | ora |
  |---|---|---|
  | 60 | 10/10, **99,7 %** del tempo fuori | 10/10, **80,9 %** |
  | 70 | 9/10 | **3/10**, 1,1 % |
  | 75–140 | 120/132/140 con 1–3 corse | **0/10 tutti** |
  | 150 / 160 | 1–3 | 1 / 2, 0,1 % |
  | **170** | **4/10, errore 50 %, 26,5 s fuori** | **1/10, 4,4 %, 0,1 %** |
- [x] **Nessuna regressione sui gate:** `VPAlign` cinque gradini protetti tutti
  PASS con gli stessi numeri (0,78–1,47 s, 23,3–24,4 ms, zero violazioni), e il
  100→140 non protetto migliora da 26,70 a 15,14. `--octave focused` 6/4 con le
  quattro RED deliberate dell'item 1, `--swing` 3/0, `--leak` 49/0.
  `probe_tempo_step`: 100→160 da 27,8 a 21,0 s; 75→140 da 8,1 a 10,7 s (peggio di
  2,6 s); 60→120 da 0,0 a 7,5 s, che **non è una regressione** — prima lo zero
  voleva dire «leggeva già 120 mentre il brano era a 60», cioè era giusto per il
  motivo sbagliato.
- [x] **Chiuso il feedback HMM che rendeva permanente il doppio a 52/60 BPM.**
  La prima misura reale dell'intervallo ora raffina un'acquisizione HMM ancora
  provvisoria; lo scatto d'ottava ricentra anche il marginale di tempo dell'HMM
  senza perdere la sua distribuzione di fase; un HMM su un'altra ottava non può
  più riscrivere da solo `anchorBpm`. Probe deterministica mirata (drift live 3
  BPM, jitter 10 ms): primo lock corretto a 52 BPM **~12,5 -> 3,48 s**, cioè
  tre quarti con il nuovo gate di contesto; su 120 s
  x 5 semi, tempo fuori tolleranza **0,4% a 52** e **1,5% a 60**; 120/160 BPM
  zero uscite su 3 semi. `VPTests --tempo-slow`: 7/0; dopo sei secondi senza
  battiti, il primo ritorno a 26,56 s è sulla fase esatta (0,000 beat). Resta
  volutamente l'ambiguità acustica dell'item 1 sui
  brani lenti con ottavi forti: lì il test ONNX 50 BPM continua a poter leggere
  100 e il comando ÷2 è la soluzione deterministica.
- [x] **Lo swing ai tempi lenti è il caso specifico dell'utente, ed è molto
  peggiore del dritto (2026-09-07).** Segnalato: *«sto ascoltando un brano live
  con swing a circa 81 bpm… tende ad accelerare o decelerare e prima di rientrare
  passano un bel po' di battute. Vorrei che agganciasse il più veloce
  possibile.»* Riprodotto generando battito forte + **ottavo swingato più
  debole** (la probe fino a qui faceva solo impulsi equidistanti):

  | materiale | 81 BPM | 96 BPM | 120 BPM |
  |---|---|---|---|
  | dritto | **1,9 s** | 1,4 s | 1,4 s |
  | swing 0,65 | **13,1 s** (peggio 14,4) | 13,5 s | 3,4 s |
  | swing 1,00 | **17,7 s** (peggio 30,0) | 10,0 s | 1,5 s |

  E stabilità: a 81 BPM il dritto ha 1 corsa su 10 con uscite, lo swing **7 su
  10** con errore peggiore 64 %.

  **Perché 120 è immune:** il livello degli ottavi a 120 sarebbe 240 BPM, oltre
  `kMaxBpm = 220`, quindi non è un'ipotesi legale. A 81 sarebbe 162, dentro. Il
  problema è **lento + swing**, non lo swing.

  **Dove vanno i 13 secondi**, misurato: l'acquisizione si aggancia al livello
  **1,5×** che lo swing implica (123 BPM su 82 veri), e poi il rientro paga due
  attese in fila — il comb non è **pronto** prima di ~8 s a 81 BPM (gli servono
  cinque periodi dell'ottava sotto: la skill lo dice, 7,9 s a 76 BPM) e poi
  servono ~6 battiti di voti, altri 4,4 s. **La metà più grande è strutturale**,
  non una soglia da stringere. Il comb intanto leggeva 82,19 a salienza 1,00 dal
  primo istante in cui poteva parlare.
- [x] **Fatto, piccolo ma coerente:** il bonus di pazienza `kOctaveSnapBeatsHealthy`
  non va più a una griglia che usa un tick sì e uno no. È la stessa
  contraddizione del punto 2 sopra, nella stessa funzione: il commento dice *«il
  doppio del tempo vero cade su ogni picco rilevato, quindi è sempre una di
  quelle sane»* e poi concedeva pazienza proprio per quello. Vale 19,0 → 17,7 s
  sullo swing pieno a 81; non è il termine dominante e non pretendo che lo sia.
- [x] **Aggancio swing risolto nel punto in cui nasce.** Il decoder ora riconosce
  la cella causale lungo-corto: sul bus diretto bastano forte-debole-forte; sul
  microfono serve il quarto picco, che corrobora la seconda cella e respinge una
  coppia transiente/riflessione. Prima della griglia il refractory usa il periodo
  più veloce legale, quindi non cancella il ritorno corto dello swing pieno; l'HMM
  non può pubblicare una risposta senza contesto un istante prima che la cella si
  chiuda. Probe mirata, ratio 0,60 e 2/3 a 52/81/96/120/168 BPM: primo tempo
  corretto in **1,04–1,40 quarti sul diretto** e **1,37–2,07 sul microfono**;
  tutti ancora corretti dopo 10 s. Il vecchio percorso da 13–18 s non parte più.
- [x] **L'1 entro due battute.** I voti di battuta si accumulano e decadono **per battito**
  (`kVoteDecay = 0.982`), quindi `voteBeats` satura a **55,6**. Con
  `kBeatsToMoveTheBar = 32` servono **47 battiti accettati** prima di poter
  spostare l'1 mentre si suona, ma la decisione d'ingresso e il rientro ora
  attraversano la soglia al **quarto numero 8**:

  | | battiti | a 60 BPM | a 80 BPM | a 120 BPM |
  |---|---|---|---|---|
  | in ingresso / rientro (soglia 7 con decay) | 8 | 8,0 s | 6,0 s | 4,0 s |
  | **in play (`kBeatsToMoveTheBar = 32`)** | **47** | **47,2 s** | 35,4 s | 23,6 s |

  Il margine di vittoria non è stato abbassato: entro due battute si agisce solo
  se l'opinione è chiara. Durante il play resta la soglia prudente di 32, perché
  cambiare il conteggio sotto una parte già udibile è un'operazione diversa.
  `VPTests --bar`: 10/0, incluso il rientro dopo un taglio di due quarti.
- [x] **100 non diventa più 50/200 e i pulsanti non saltano due ottave.** La
  vecchia cadenza a soglia interpretava due downbeat mancati a distanza di otto
  quarti come prova di una griglia doppia: su un 100 chiarissimo poteva quindi
  pubblicare il suggerimento 50. Quel discriminante, già dimostrato ambiguo, è
  ora davvero spento. Inoltre ÷2/×2 avanzano di un livello dal valore
  **effettivamente visualizzato**, non impostano più −1/+1 assoluti: da 50 si va
  a 100 e poi 200; da 200 si torna a 100 e poi 50. Regressione mirata dentro
  `VPTests --tempo-slow`: 100 resta 100 con downbeat alterni mancanti e le quattro
  transizioni 100↔50 / 100↔200 passano tutte da 100.

---

### 23. Il regime FISSO si congelava su una band che deriva 🟡 (2026-09-07, trovato e corretto — resta ascolto)

Richiesta dell'utente: *«verifica e potenzia al massimo il riconoscimento dei bpm
nel modo più veloce possibile, e se sta uscendo dal tempo fallo rientrare subito,
come un perfetto percussionista capirebbe al volo»*.

**Prima cosa, per chi legge dopo:** durante questa sessione HEAD si è mosso
(commit `7292ffb` e `74fe5fc`), quindi le misure degli item 21 e 22 sono contro
un albero più vecchio. Il tabellone qui sotto è il tree attuale.

**Banco nuovo: `scripts/probe_matrix.cpp`.** Aggancio *e* stabilità, su
dritto / swing 0,65 / swing 1,0 × 60…170 BPM × 10 semi. Genera anche l'**ottavo
swingato**, che `probe_steady_tempo` non fa e che è la metà del problema
dell'utente. Non è in CMake, si compila in due secondi (comando nel suo header).

Stato del tree **prima** di questo item: **aggancio medio 1,78 s** su tutta la
matrice, righe swingate già pulite, 60 BPM che non raddoppia più. Restavano **5
uscite su 210 corse**, tutte al 4,2–4,4 %, da 1,4 a 10 secondi.

**Tracciata la peggiore** (dritto 81 BPM, seme 7, 10 s fuori) e non era niente di
quello che avevo corretto finora:

```
t=20.5  pubbl=82.09 (vero 80.87)  reg=FISSO  comb=82.08  lungo=82.20  corto=81.79
t=23.4  pubbl=82.09 (vero 80.23)  reg=FISSO  comb=81.30  lungo=82.01  corto=80.93
```

Il pubblicato **congelato a 82,09** mentre il tempo vero scende. Comb, fit lungo
e fit corto lo seguono giù tutti e tre: **solo il numero pubblicato non si
muove.** Il decoder aveva deciso «disco tagliato a click» su una band che deriva
di 3 BPM — entrando in FISSO in un tratto piatto della deriva — e poi ci è
rimasto. Su un brano **live** è il guasto peggiore che ci sia: l'app smette di
seguire la band.

- [x] **Difetto 1: il contatore d'uscita si azzerava al primo battito buono.**
  `if (wandered) ++fixedErrorBeats; else fixedErrorBeats = 0;` e l'uscita ne
  vuole 6 **consecutivi**. Su una deriva l'errore oscilla attorno alla soglia,
  quindi i sei consecutivi non arrivavano mai. È **la stessa identica lezione**
  che lo scatto d'ottava ha già imparato duecento righe più giù, col suo
  commento: *«un contatore che si azzerava al primo battito d'accordo non
  arrivava da nessuna parte… è così che un livello scelto nei primi secondi
  sopravviveva a ogni correzione.»* Ora decrementa invece di azzerare.
- [x] **Difetto 2, quello che conta: l'uscita da FISSO guardava solo la
  *dimensione* dell'errore, mai la *direzione*.** Tutti e quattro i termini
  d'uscita sono «quanto ci siamo allontanati», e su una deriva la dimensione
  arriva tardi per costruzione — l'errore parte da zero e cresce, e quando il 2 %
  si è accumulato il musicista è fuori da secondi. Il flag `moving` («gli estremi
  della finestra differiscono, e più della sua stessa dispersione, quindi è una
  direzione e non rumore») era già calcolato lì sopra e **usato solo in
  acquisizione**. Ora libera anche FISSO. Un disco a click non può produrlo; una
  band lo produce prima che l'errore si senta.
- [x] **Effetto:** dritto 81 BPM da **1/10 corse con uscite (10 s fuori al
  4,3 %) a 0/10**. Totale 5 → 4 uscite, tempo fuori medio **0,06 % → 0,03 %**.
  E le **rampe migliorano** in `VPAlign`: fase 20,8 → 18,8 ms sulla rampa da 4 s
  e 8,0 → 6,0 ms su quella da 12 s — coerente, uscire da FISSO su un tempo che si
  muove vuol dire seguirlo meglio.
- [x] **La proprietà opposta è intatta, misurata apposta:** su click track vero
  (deriva 0, jitter 0,5 e 2 ms, 81/96/120/140 BPM, 8 semi) errore medio
  **0,004–0,023 % e zero uscite, identico a HEAD riga per riga**. `moving` non
  scatta su un click.
- [x] **Gate:** `probe_tempo_step` identico a HEAD su tutte e 11 le righe;
  `VPAlign` cinque gradini protetti identici (0,78–1,47 s, 23,4–24,4 ms, zero
  violazioni di impulsi); `--octave focused` 6/4 con le quattro RED deliberate
  dell'item 1; `--swing` 3/0; `--leak` 49/0.
- [ ] **Restano 4 uscite su 210**, tutte su materiale **dritto**: 60 BPM 3/10 e
  170 BPM 1/10, al 4,2–4,4 %. Il materiale dell'utente (lento e swingato) è
  **pulito 0/10 su tutte le righe**. Non le ho inseguite: sono ai due bordi della
  gamma e valgono lo 0,03 % del tempo.
- [x] **Il banco stretto dava numeri falsamente rassicuranti, e l'utente ha avuto
  ragione a rifiutare la taratura su un brano preciso.** Chiesto esplicitamente:
  *«non voglio venga fatto su un brano preciso, ma sempre, sia con brani caricati
  sia in live»*. Giusto, ed è anche quello che dice il resto di questo file. Il
  banco è stato allargato da due assi (tempo, swing) a **dodici materiali** —
  `scripts/probe_matrix.cpp`, con densità della suddivisione, accenti per quarto,
  jitter, battiti ingoiati dal mix e vuoti d'arrangiamento. Il quadro cambia:

  | materiale | aggancio med / peg | uscite | %fuori |
  |---|---|---|---|
  | metronomo | 2,31 / 6,98 | 1/30 | 0,05 % |
  | **rock a ottavi** | **7,30 / 29,80** | **9/30** | **8,30 %** |
  | rock a sedicesimi | 2,41 / 6,98 | 1/30 | 0,03 % |
  | backbeat secco | 4,27 / 17,36 | 9/30 | 0,42 % |
  | **half-time** | **mai** | **30/30** | **98,95 %** |
  | swing 8vi / shuffle 16mi / swing pieno | 2,5–4,4 | 1–3/30 | ≤0,10 % |
  | **band larga (25 ms)** | 7,53 / 31,84 | 10/30 | 0,68 % |
  | **mix che ingoia (18 %)** | **9,05 / 49,40** | **16/30** | 4,54 % |
  | **con vuoti (1 battuta su 4)** | 9,61 / 35,46 | 9/30 | 2,00 % |
  | solo accordi | 4,97 / 19,56 | 10/30 | 1,31 % |

  **Aggancio medio reale 5,30 s, non 1,78.**
- [x] **Come sbaglia, non solo quanto** (`probe_matrix --ratio`, rapporto medio
  riportato/vero). Quasi tutto legge **1,00**, cioè il livello giusto — il che è
  la notizia buona. Due eccezioni:
  - **rock a ottavi a 81 BPM: 1,44.** Media fra corse giuste e corse sull'ottava
    sbagliata. È lo **stesso caso** dello swing lento dell'item 22: tempo lento +
    suddivisione riempita, dove il livello degli ottavi (162) è ancora dentro la
    gamma. A 100 BPM e oltre sparisce, perché sarebbe ≥200.
  - **half-time: 2,02 ai lenti, 0,50 ai veloci.** È l'ambiguità metà/doppio
    dell'item 1, **e in più la fixture è ingiusta per una probe solo-decoder**:
    la convenzione che tiene il polso nella gamma di un percussionista vive in
    `BeatTracker::updateAutoOctave`, che questa probe non esercita. Non leggere
    il 98,95 % come un guasto senza prima rifarlo end-to-end.
- [x] **Le due modifiche di questo item verificate anche sul banco largo**, A/B
  contro `git show HEAD:`: uscite totali **104 → 101**, fuori medio
  **9,83 % → 9,70 %**, aggancio medio invariato, e **nessuna cella peggiora**
  (metronomo 2/30→1/30, rock 16mi 2/30→1/30, mix che ingoia 17/30→16/30,
  half-time 99,49→98,95 %). Miglioramento piccolo e pulito, non una svolta.
- [ ] **La classifica di cosa attaccare dopo, in ordine di quanto pesa.** Tutte e
  tre le prime sono *acquisizione*, non recupero:
  1. ~~**Rock a ottavi ai tempi lenti (~81 BPM): 8,30 % del tempo fuori.**~~
     **FATTO (2026-09-07), vedi sotto.**
  2. **Mix che ingoia battiti: aggancio 9,05 s, peggiore 49,4 s.** Un missaggio
     vero non consegna ogni battito.
  3. **Vuoti d'arrangiamento e band larghe: 9,6 e 7,5 s.**
- [x] **Punto 1 chiuso: il charleston sugli ottavi teneva la griglia un'ottava
  sopra, per sempre.** Tracciato `rock 8vi` a 81 BPM, seme 2:

  ```
  t=11.1  pubbl=163.85 (vero 82.48)  comb=82.08 sal=1.00  lungo=163.87  mism=0  gap=1.0
  t=59.4  pubbl=164.67 (vero 81.15)  comb=82.30 sal=1.00  lungo=164.58  mism=0  gap=1.0
  ```

  Il fold diceva **82 a salienza 1,00 per un minuto intero**, il committed stava
  a **164** — un'ottava esatta — e `octaveMismatch` non lasciava mai lo zero.
  Causa: il veto `unprovenSlowerOctave`. Il `gridIsDense` dell'item 22 prende la
  griglia raddoppiata **quando fra i battiti c'è silenzio**; qui il charleston
  *riempie* quei tick, quindi la griglia sbagliata è densa, coverage 1,00,
  residuo 0,03, salto d'indice 1,0 — ogni prova che il veto possiede dice che la
  griglia va bene.
- [x] **Fix: quello che le due griglie non condividono è il *peso* dei battiti.**
  Sul polso sono tutti battiti; un'ottava sopra, uno sì e uno no è un charleston.
  Nuovo `BeatDecoder::recentStrengthAlternation()` — mediane per parità sugli
  ultimi 12 battiti accettati, misurato **0,1–0,2 su una griglia giusta contro
  ~0,5 su una costruita sulla suddivisione**. Il veto si stende quando
  l'alternanza supera 0,35 **e** il fold nomina qualcosa entro il 20 % di metà
  del committed (`kSubdivisionAlternation`, `halfError`). **Solo toglie il
  veto**: lo scatto continua a volere la salienza del fold e i suoi `snapBeats`
  di voti.
- [x] **Effetto sul banco (12 materiali × 5 tempi × 6 semi):**

  | | prima | dopo |
  |---|---|---|
  | **rock 8vi, tempo fuori** | **8,30 %** | **0,65 %** |
  | rock 8vi, rapporto a 81 BPM | **1,44** | **1,00** |
  | con vuoti | 2,00 %, agg 9,61 s | 1,50 %, agg 8,72 s |
  | mix che ingoia | 4,54 % | 4,38 % |
  | mai-agganciato | 33 | 30 |
  | **fuori medio, tutto il banco** | **9,70 %** | **9,01 %** |

  **Nessuna cella peggiora**, e nella tabella dei rapporti **ogni riga tranne
  half-time legge adesso 1,00 a tutti e cinque i tempi**.
- [x] **Gate, tutti verdi e tutti A/B contro `git show HEAD:`:** `probe_tempo_step`
  **identico riga per riga**; click track (deriva 0, jitter 0,5 e 2 ms)
  **identico riga per riga** — l'alternanza non scatta su un click; `VPAlign`
  cinque gradini protetti tutti PASS con gli stessi numeri (0,78–1,47 s,
  23,4–24,4 ms, zero violazioni di impulsi) e rampe PASS; `--octave focused` 6/4
  con le quattro RED deliberate dell'item 1; `--swing` 3/0; `--leak` 49/0.
- [ ] **Segnalazione, non mia:** `VPTests --bar` è **9/1 sul tree attuale** —
  `within two bars of the return the one is beat zero again` — ed è rosso anche
  con le mie modifiche tolte (verificato con `git stash`). Preesistente, item 2.
- [ ] **Restano, nell'ordine:** `mix che ingoia` (aggancio 8,98 s, peggiore
  49,4 s, 4,38 % fuori — un missaggio vero non consegna ogni battito),
  `band larga` a 25 ms (7,53 s, 0,68 %), `con vuoti` (8,72 s). E half-time, che
  però prima di essere chiamato guasto va rifatto **end-to-end**: la convenzione
  che lo decide sta in `BeatTracker::updateAutoOctave`, che questa probe non
  esercita.

- [ ] **Non tarare niente di tutto questo su un brano solo.** Il banco esiste
  apposta; ogni modifica va misurata su tutte e dodici le righe **e** su
  `probe_tempo_step` + `VPAlign`, come queste due.
- [ ] **Ascolto.** È l'unica verifica che manca: rimettere lo stesso brano live
  swingato a 81 e dire se il congelamento si sente ancora.

---

### 24. A basso livello l'acquisizione prende un'ottava alta falsa 🟡 (2026-09-08, causa trovata e corretta a -12 dB — resta -18 dB e l'ascolto)

Riprodotto sulla registrazione dell'utente (`3 INFINITO.mp3`, estratto 30-120 s,
riferimento 91 BPM). Stesso contenuto musicale, solo il livello cambia:

| livello | avvio | `FISSO` | media finestra | deriva media / max |
|---|---|---|---|---|
| `-f 65536` (caldo) | 91 subito | — | 91.06 | 5.3 / 19.0 ms |
| `-f 8192` (circa -12 dB) | 212.6 BPM, snap a 91.19 solo a 12 s | ~20 s | 94.94 | 15.7 / 257.7 ms |

Non e' "il volume alto fa perdere il tempo": e' il **livello basso** che lascia
passare una falsa ottava alta durante l'acquisizione. Tenuto il tempo, il
mantenimento e' buono in entrambi i casi.

**Misurato il frontend (nuovo `VPActivations --wav ... --sweep`).** Primi 12 s
dell'estratto, guadagno applicato **solo alla copia destinata al modello**:

```bash
cmake --build build-host --target VPActivations -j4
./build-host/VPActivations_artefacts/Release/VPActivations \
  --wav /tmp/vp-infinito-30-120.wav --sweep --secs 12 --gains 0,-6,-12,-18
```

| gain | rms | magMean | diffMean | pBeat>0.5 | pDown medio | pDown>0.5 |
|---|---|---|---|---|---|---|
| 0 dB | -19.5 | 0.2395 | 0.0301 | 33 | 0.035 | 17 |
| -6 | -25.5 | 0.1595 | 0.0216 | 23 | 0.084 | 30 |
| -12 | -31.5 | 0.1020 | 0.0148 | 15 | 0.144 | 57 |
| -18 | -37.5 | 0.0624 | 0.0096 | 3 | 0.169 | 67 |

I fotogrammi di battito confidenti **crollano da 33 a 3** e le attivazioni di
downbeat **quintuplicano**: al decoder arriva un'evidenza diversa, non piu'
debole in modo uniforme.

**Il frontend non ha un bug di normalizzazione.** Verificato con un seno a
1 kHz a fondo scala: `magMax = 2.2666`, cioe' `log10(1 + 183.6)`, contro il
riferimento madmom analitico `|X|` di picco `352.1` (finestra `/ 32767`, rfft
non normalizzata) ridotto dal filtro triangolare a somma 1. Scala, finestra e
filterbank coincidono con BeatNet/madmom. madmom non e' installato qui, quindi
**non** e' stato fatto un confronto bit-a-bit con l'implementazione originale.

La dipendenza dal livello e' **intrinseca a `log10(1 + x)`**: lo stesso seno
scende da 2.2666 a 1.3826 a -18 dB. Nessuna AGC nel bus audio; un eventuale
condizionamento riguarda solo la copia del segnale per il modello.

- [x] **Arbitraggio iniziale corretto (2026-09-08).** Causa: `tryFastAcquire`.
  A -12 dB i picchi distavano 141 ms (424 BPM); il test di alternanza concludeva
  giustamente «è una suddivisione» e raddoppiava **una sola volta**, a 212, che
  supera lo stesso test. I rami a coppie e ad alternanza azzerano `bestError`,
  disattivando il confronto con lo state space — che stava dicendo 103.45, un
  ottava esatta di distanza. Fix: sopra `kFastAcquireVetoBpm` (180) lo state
  space conserva un veto oltre `kOctaveThreshold`. Sotto la banda nulla cambia.

  | livello | prima | dopo | deriva media | peggiore |
  |---|---|---|---|---|
  | clip / 0 / -6 dB | corretto | invariato | invariata | invariata |
  | **-12 dB** | 94.92 BPM | **90.96** | 16.3 → **9.5 ms** | 262.5 → **139.4 ms** |
  | -18 dB | 121.10 | 121.06 | invariata | invariata |

  Regressioni prima/dopo: `probe_matrix` (360 corse) **identico byte per byte**,
  `probe_tempo_step` identico, `VPAlign` identico, `--tempo-slow` 10 PASS,
  `--octave` 7/4 identico (i 4 fallimenti a 50 BPM sono l'item 1, preesistenti).
- [x] Aggiunto `VPLive --gain <dB>`: la prova di livello è un ciclo su un solo
  file, senza pre-renderizzare WAV con mpg123.
- [ ] **-18 dB resta rotto, ed è un altro guasto**: aggancia 91 entro 12 s, poi
  è il **pettine** a saltare a 182.37 per quattordici secondi prima di tornare.
  Salto d'ottava dopo l'acquisizione, sorgenti concordi sul valore sbagliato.
- [x] **Regressione mirata del livello (2026-09-08): `VPTests --level`.**
  Kit sintetico normalizzato a picco 0.9, poi 0/-6/-12/-18 dB e caso clippato,
  su 52/91/168 BPM; 38 s a corsa con un vuoto di 2 s a 26 s. Cinque numeri
  separati — ottava (`|log2| < 0.25` tenuta due beat), ingresso, `FISSO`, fase
  segnata meno l'anticipo, rientro — più `bpm@4s` per sapere *quale* livello
  sbagliato stava leggendo. Non è nella suite completa: 15 corse sono ~4:46.
  `--level 52|91|168` per un tempo solo. Due corse danno numeri identici.

  Esito **13 PASS / 3 FAIL**. 168 BPM è pulito a ogni livello pulito (ottava
  0.29 s, ingresso 0.41 s, fase 6.5 ms). 91 BPM a -12 dB aggancia in 3.35 s,
  dentro le due battute: la correzione qui sopra vista dal banco sintetico.
- [x] **Corretta la pazienza dello snap d'ottava (2026-09-08).** Le due
  concessioni `kOctaveSnapBeatsHealthy` e il bonus di anzianità si applicavano a
  qualsiasi disaccordo oltre 0.25 ottave. Esistono per l'ambiguità **d'ottava**:
  una griglia doppia cade su ogni battito rilevato, quindi sembra sempre sana.
  Un disaccordo lontano da un numero intero di ottave non ha quella scusa —
  67.7 e 90.9 non sono la stessa pulsazione contata in due modi. Ora entrambe
  sono subordinate a `octaveArgument` (`kOctaveArgumentTolerance` 0.15).

  Guadagno molto più largo del caso che l'ha fatta trovare:

  | banco | prima | dopo |
  |---|---|---|
  | `probe_tempo_step` 120→150 | 17.2 s | **12.8** |
  | 100→160 | 13.5 s | **10.1** |
  | 160→100 | 15.0 s | **12.0** |
  | 90→120 | 26.7 s | **20.7** |
  | `probe_matrix` aggancio medio | 5.25 s | **5.20** |
  | `probe_matrix` uscite | 101 | **99** |
  | `VPAlign` gradino 100→140 | 15.14 s | **11.26** |

  `swing 8vi` passa da 1/30 a **0/30** uscite, `swing pieno` da 3/30 a 2/30.
  Nessuna riga peggiora. `--octave` 7/4 identico, `--tempo-slow` 10 PASS.
- [ ] **91 BPM a 0 dB: causa trovata, non corretta.** `tryFastAcquire` pubblica
  **64.55** a 2.58 s mentre lo state space nomina **115.38 con margine -0.174**
  — non incerto, contrario. È la stessa forma del bug del punto 2 ma sotto i
  180 BPM: qui a disattivare il controllo di livello è la regola `rawBpm < 90`,
  che prende per buona la spaziatura osservata per non ripetere 76 → 152.
  Stringerla su una sola riga sintetica è ciò che l'item 23 vieta, e quella
  regola è l'unica cosa che tiene 76 lontano da 152: serve materiale reale a
  più livelli. Con la correzione qui sopra: 17.71 → **15.67 s**, ancora FAIL.
- [x] **Corretto (2026-09-08): il decoder ora tiene il livello sotto una parte
  che suona.** Il caso era 168 clippato — quattordici secondi di griglia sana
  (res 0.014, settled) e poi **il pettine stesso** a 84.03, con il decoder che
  lo segue. Stessa cosa dall'estremo opposto: la registrazione a -18 dB aggancia
  91 e poi va a 182.37 per quattordici secondi. Le sorgenti sono d'accordo fra
  loro sul valore sbagliato: sbagliato è il **momento**, non l'evidenza.

  `updateAutoOctave` rifiutava già di muovere il livello sotto una parte che
  suona, ma poteva farlo solo sul proprio shift: se è il decoder a dimezzare il
  tempo riportato, `autoOctave` resta a zero e la griglia si sposta lo stesso.
  Ora `sounding` arriva al decoder (`BeatTracker` → `NeuralBeatTracker` →
  `BeatDecoder`, stesso passaggio dell'ottava utente) e il voto dello snap si
  ferma su `sounding && !provisional && octaveArgument`.

  | banco | prima | dopo |
  |---|---|---|
  | `VPTests --level` | 13 PASS / 3 FAIL | **15 / 1** |
  | 168 clippato, finale | 84.04 | **168.03** |
  | `VPLive --gain -18`, BPM medio | 121.06 (+33%) | **91.08 (+0.09%)** |

  `--octave` 7/4 identico (÷2 e ×2 compresi), `--tempo-slow` 10 PASS,
  `--state-timing` 3 PASS, `VPAlign` identico. `probe_matrix` e
  `probe_tempo_step` identici **perché pilotano il decoder da soli e non
  chiamano mai `setSounding`**: non possono regredire e non possono validare.
- [ ] Scambio accettato: una **sezione half-time vera** dentro il brano ora
  viene rifiutata come cambio d'ottava. È lo stesso scambio dell'item 17, esteso
  al decoder; ÷2 / ×2 restano la via manuale. Da ascoltare su materiale che ne
  ha una.
- [ ] A -18 dB la deriva media resta **61.6 ms** (max 349.3): il tempo ora è
  giusto per tutta la corsa, ma la fase a quel livello è povera lo stesso. È il
  degrado che la colonna «fase» misura, non l'ottava.
- [x] **`FISSO` non era un terzo difetto** — etichetta mia sbagliata alla prima
  lettura. A 91 BPM: -6, -18 e clip raggiungono `fixed`; 0 dB resta `unknown`
  come conseguenza del punto sopra; -12 dB va in **`live`**, cioè su un tempo
  costante decide che si muove, coerente con i suoi 32.6 ms di fase. `mayFix`
  chiede `lastFitResidual < 0.05` e una finestra assestata: `FISSO` segue la
  qualità dell'aggancio.
- [ ] **La fase peggiora col livello a 91 BPM**: 2.8 ms a 0 dB contro 26-33 ms
  a -6/-12/-18. A 168 BPM non succede. Serve prima la verità di fase annotata
  sulla registrazione per sapere se conta davvero.
- [ ] 52 BPM legge 104 a **ogni** livello: è l'item 1, non il guadagno.
  Misurato e deliberatamente **non** asserito nel filtro.
- [ ] Verita' di fase annotata sulla registrazione (30-120 / 120-210 / 210-250 s)
  prima di credere ai picchi isolati da 85-167 ms.
- [ ] iPad, con variazione del gain hardware.

Dettaglio completo e comandi in `docs/HANDOFF_TEMPO.md`.

---

### 25. Il volume d'ingresso: fader inguidabile e barra che non dice niente 🟢 (2026-09-08, corretto, visto su Mac e provato su iPad)

Segnalazione dell'utente: *«il volume di ingresso... anche la barra del volume,
perche' e' estremamente sensibile e non si capisce quale potrebbe essere il
volume piu corretto di ingresso»*. Due difetti distinti, con la stessa causa:
nessuno dei due era in dB.

**La barra.** Era `sqrt(picco) * 3.2`, che si riempie a **-20 dBFS**: qualunque
livello che un palco produce davvero la mandava a fondo scala, quindi l'unica
cosa che sapeva dire era «sta arrivando qualcosa». Ora e' una scala in dB su 48,
con una **striscia chiara** disegnata dove l'analisi vuole stare, presa dalle
misure dell'item 24:

- sotto circa **-18 dBFS di picco** la rete e' fuori dal livello su cui e' stata
  addestrata e il tempo se ne va con lei (la registrazione reale a quel livello
  passava dieci secondi su un'ottava alta falsa);
- sopra circa **-1 dBFS** la guardia di clipping dell'analisi
  (`kMakeupClipGuardPeak`, item 16) inizia a riportare giu' il segnale, quindi
  alzare ancora non compra piu' niente.

Banda a `kInputLowPeak` 0.2512 (-12 dBFS) .. `kInputHighPeak` 0.8913 (-1 dBFS),
cioe' dal 75.0% al 97.9% della larghezza. Riempimento grigio sotto, fuchsia
dentro, ambra sopra, con due tacche sui bordi. Tenuta di picco con rilascio
lento (salita immediata, ~20 dB al secondo in discesa): a 15 fps il picco grezzo
di una band sfarfalla di 20 dB fra un colpo e il vuoto dopo, e una barra che
sfarfalla non si legge contro una banda.

La frase sotto la barra diceva «SENTO LA STANZA», che rispondeva a un'altra
domanda. Ora dice **IN ASCOLTO / MIC BASSO, ALZA / LIVELLO OK / MIC ALTO,
ABBASSA**.

**Il fader MIC.** Era lineare in ampiezza su 0..2 con 180 punti di trascinamento:
un punto di dito valeva 0.10 dB all'unita', **0.92 dB a -20 dBFS e 2.13 dB a
-28** — cioe' la parte piu' nervosa del controllo era proprio quella dove uno
sta cercando, perche' e' li' che il livello e' troppo basso. Ora:

| | prima | dopo |
|---|---|---|
| corsa | 180 punti | 600 |
| scala | lineare 0..2 (+6 dB max) | unita' a meta' corsa, 0..4 (+12 dB max) |
| lettura | `100%` | `+0.0 dB` |
| dB per punto a 0 dB | 0.10 | **0.06** |
| dB per punto a -20 dBFS | 0.92 | **0.18** |
| dB per punto a -28 dBFS | 2.13 | **0.28** |

Da 1.6 a 7.6 volte piu' fermo, e piu' fermo dove prima era peggio. Doppio tocco
riporta a unita' come prima. Il tetto a +12 dB serve perche' una mandata a
-30 dBFS non arrivava alla banda con +6. Il motore gia' limita a 4.

Aggiunta una riga alla nota INPUT delle impostazioni: alzare finche' la barra
entra nella striscia.

- [x] Codice: `Source/UI/MainComponent.cpp` (`meterPosition`, `micGainText`,
  `kInputLowPeak`/`kInputHighPeak`, `micHold`), `MainComponent.h`.
- [x] Mappatura verificata numericamente (tabella qui sopra). Compila.
- [x] **Visto sul Mac (2026-09-08)**, tema chiaro e scuro, con il brano
  dell'utente caricato in BRANO: manopola `0.0 dB` con lancetta a ore 12,
  striscia e tacche leggibili su entrambi i fondi, barra al 62% con verdetto
  `MIC BASSO, ALZA` (il file entra a circa -18 dBFS di picco). I tre stati e i
  tre colori verificati dall'utente muovendo la manopola.
- [x] Due difetti trovati **guardandolo** e corretti subito: il gradiente
  finiva in `text()`, bianco in tema scuro, quindi la punta della barra era
  bianca in tutti e tre gli stati e il colore sopravviveva solo a sinistra (ora
  il colore sta sulla punta); e in BRANO il verdetto non compariva affatto,
  perche' la riga scriveva `BRANO DIRETTO`, che ripeteva l'etichetta accanto.
- [x] **iPad provato (2026-09-08)**: l'utente riferisce «sembra ok». È un
  giudizio a orecchio e a occhio, non una misura: non sono stati registrati
  livello, tempo alla prima ottava corretta né ingresso. Se serve un numero da
  quella prova, va rifatta registrando la mandata.
- [ ] Resta da vedere su una mandata con **gain hardware che cambia in corsa**
  e con il ritorno acustico delle percussioni accese: il banco host non simula
  microfono, stanza, latenza I/O né feedback degli strumenti.
- [ ] La banda e' in **picco**. Sulla registrazione reale il picco naturale era
  -4 dBFS con RMS -19.5 (fattore di cresta 15 dB); un segnale molto compresso
  entrera' in banda con un RMS piu' alto. Se sul palco la striscia risulta
  ottimista, la soglia giusta e' l'RMS, non il picco.

---

### 26. `VPTests --bar` falliva da settimane: era il test 🟢 (2026-09-08, corretto il test, codice invariato)

«within two bars of the return the one is beat zero again» falliva identico a
`e5cc06e`, `11618cb`, `aa97ced`, `9fcaf02` e `dcff368` — ricostruiti uno per uno.

`barPhase * 4` è la posizione nella battuta **all'istante in cui si guarda**, e
lo stub mette l'uno dove `(beatNo + 2) % 4 == 0`. Lo zero era vero finché la
finestra di recupero era `fourBarsSec + 1.0` (27.797 s → battito 46, e 46+2 è
multiplo di 4); il commit `74fe5fc` l'ha stretta a `twoBarsSec` senza
ricontrollare dove si cadeva (22.0 s → battito 36, quindi 2).

Con la finestra resa variabile, il conteggio ha coinciso con `(beatNo + 2) % 4`
a **sei lunghezze su sei** da 4.8 a 10.6 s. Il test ora calcola l'atteso come fa
già il gemello `bar-seek`. **10 PASS / 0 FAIL**, tre corse identiche, nessun
codice di produzione toccato.

**L'1 che sembrava non allinearsi da solo: si allinea, dopo dodici battute.**
Prima del buco il conteggio legge 3 dove l'uno del modello è su 2, con `rot=0`.
Avevo scritto che non si allineava mai: è sbagliato. Tracciando `tryAlignFrom`
l'opinione è schiacciante subito (`best=1 bestV=0.92 runner=0.03`) e a fermarla
è solo `voteBeats`, che non arriva a `kBeatsToMoveTheBar`. Spostando il buco, la
rotazione cade fra i **46.7 e i 53.3 battiti**, che è esattamente il conto:
`voteBeats = voteBeats * 0.982 + 1` converge a 55.6 e attraversa 32 al
**quarantasettesimo battito** — dodici battute, 29 s a 100 BPM, 59 s a 50.

Non è un difetto: è il prezzo voluto di spostare una battuta che si sente.
Mancava solo che il numero fosse scritto — «32» non dice «dodici battute». Ora è
annotato accanto alla costante, con la misura, e il test stampa anche dove l'uno
**è** prima del buco.

- [ ] **Decisione tua, non un bug**: se l'app entra sull'1 sbagliato ci mette
  mezzo minuto a correggersi da sola (quasi un minuto a 50 BPM). «SPOSTA L'1» è
  lì per quello. Se mezzo minuto è troppo, il numero da muovere è
  `kBeatsToMoveTheBar` — ma abbassarlo rende la battuta più mobile sotto le mani
  di chi suona, che è il motivo per cui è alto.

---

### 27. Gradini piccoli di tempo: seguire un cambio di un paio di BPM 🟡 (2026-09-08, misurato e migliorato — resta l'ascolto)

Richiesta: *«far seguire e tenere ancora meglio qualsiasi tempo, anche con un
cambio di un paio di BPM improvviso»*, entro circa due quarti, con rientro
immediato dalla deriva.

Il gate di astra (regime `fixed`, feed diretto, due fit di quattro battiti che
devono essere puliti e consecutivi) riscriveva `bpm` e basta: il clock ci
arrivava con la sua costante di tempo, quindi un gradino già dimostrato atterrava
come una pendenza. Ora pubblica una transizione `rapid` confermata, la stessa dei
salti grandi. Banco: `scripts/probe_small_steps.cpp` (**da aggiungere a git**).

`probe_small_steps` da **6 PASS / 5 FAIL a 8 PASS / 3 FAIL**; 168→170 da FAIL
2.977 s a PASS 1.077 s, 52→54 da FAIL 5.975 s a PASS 4.395 s. Il gate scatta
**3.1 battiti dopo il cambio** a 52, 120 e 168, in entrambi i sensi: è il minimo
teorico, con un intervallo solo non si può sapere che il tempo è cambiato.

`probe_matrix` completo (360 corse) identico byte per byte, `--steady` identico,
`probe_tempo_step` identico, `VPAlign` identico, `probe_recovery` PASS. Il banco
usa il feed diretto e il gate non è **mai** scattato: distingue un gradino da una
deriva musicale di 3 BPM al minuto.

**Ipotesi sbagliata, misurata e scartata:** `beginTempoTransition` azzera sempre
il recupero di fase; sembrava la causa del ritardo residuo. Reso condizionato ai
3 BPM che quella funzione già usa per il trim: **numeri identici riga per riga**.
Ripristinato, niente rimasto in albero.

**Dove vanno i secondi** (52→50): il clock resta sul vecchio tempo per 3.6 s e
l'errore cresce fino a 137 ms; il gate scatta, il tempo è giusto, e la fase
rientra del ~79% per battito fino a fermarsi su 11.4 ms. A 168→170 lo stesso
gradino accumula solo 12.6 ms e non esce mai dai 25 ms. **Non è una costante da
stringere, è geometria**: la finestra di rilevamento è 3.1 *battiti*, quindi
l'errore in millisecondi cresce col quadrato della durata del battito.

- [x] A 120 e 168 BPM il rientro è già immediato (1.1-1.5 s, fase sempre sotto
  i 25 ms).
- [ ] A 52 BPM servono ~5 battiti in tutto, tre dei quali sono il minimo
  teorico. Il criterio di PASS del banco (stabile entro **4** battiti dal
  cambio) è sotto il pavimento: da riscrivere in battiti-dopo-il-rilevamento,
  oppure accettare i due FAIL a ±2 BPM per quello che sono.
- [ ] I salti da ±12 BPM restano lenti (9.3 s a 132, 108 non si stabilizza):
  è il percorso della transizione ordinaria, non questo gate.
- [ ] Il gate è **solo su feed diretto**. Su microfono non è mai stato provato e
  non va abilitato senza una misura sua.
- [ ] Ascolto: un brano che cambia di due BPM a metà, a tempo lento.

---

### 28. I due suoni che non erano quello strumento: clap e cembalo 🟡 (2026-09-08/09, corretti e misurati — resta l'ascolto)

Due richieste dell'utente, stessa forma di difetto: una voce trattata come se
fosse un altro strumento.

**Il clap** (2026-09-08, già committato). Era il campione VCSL *Claps*: una
registrazione buona e lo strumento sbagliato, una stanza di gente che batte le
mani **una volta**. Centrato a 3953 Hz con il 65% dell'energia fra 2 e 6 kHz e
il 20% nel corpo — un «tss», e perfettamente mono. Ora `synthesizeClap` (i tre
`Assets/Percussion/clap*.wav` non vengono più letti, `kStem` è `nullptr`):

| | vecchio | nuovo |
|---|---|---|
| colpi | 8, 12, 22, 26 ms | 2, 8, 12, **20** ms |
| coda a -30 dB | 86 ms | **186 ms** |
| centroide | 3953 Hz | **2293 Hz** |
| energia 0.5-2 kHz | 19.9% | **74.1%** |
| correlazione L/R | +1.00 (mono) | **+0.65** |

Quattro mani dentro 20 ms con spaziatura che si stringe con la forza e jitter
per round-robin; corpo su due passabanda a 920 e 1260 Hz su rumori indipendenti
(le mani a coppa sono un risuonatore di Helmholtz); 2 ms di schiocco a 3.3 kHz
sopra ogni colpo; coda decorrelata fra i canali. Compensazione d'attacco 15.58 ms
contro i 10.12 del campione: il colpo udibile resta sul beat.

**Il cembalo** (2026-09-09). Qui il campione era **già vero** — tamburello VCSL
— ma `layerFromRecording` rilegge ogni registrazione a `kDrumTune` (2^(10/12) =
1.782) escludendo solo lo shaker. Quella costante è una decisione **sulle
congas** e lo dice dove è definita. Un tamburello non ha una pelle da accordare.

| | prima | dopo |
|---|---|---|
| parziali dominanti | 11.84 kHz | **6.64 kHz** |
| centroide | 14299 Hz | **9719 Hz** |
| energia 4-10 kHz | 7.5% | **62.1%** |
| energia oltre 16 kHz | 24.8% | **3.6%** |
| coda a -30 dB | 35 ms | **60 ms** |

Nei file sorgente i sonagli stanno a 6.3 kHz, quattro quinti dell'energia fra 4
e 10 kHz e **zero** sotto il kilohertz; a ×1.782 finivano a 11.2 kHz, un quarto
sopra i 16 kHz dove quasi nessuno sente, e i 200 ms di risonanza diventavano 112.

Correzione in due righe, nessuna modifica logica: il cembalo escluso da
`kDrumTune` come lo shaker, e stessa banda dello shaker (3-12 kHz invece di
1.6-6.8) per l'attenuazione dei colpi piani — un polo a 1600 Hz su uno strumento
senza corpo non toglie il vertice del colpo, toglie il colpo. L'argomento «è un
numero solo, così le due metà del banco restano accordate» non si applica:
`synthesizeCymbal` è scritta in hertz assoluti e non ha mai usato `kDrumTune`.
Attacco da 1.12 a 1.96 ms (down) e da 2.79 a 5.38 ms (up), sempre molto sotto il
clap che fissa l'anticipo globale: il cembalo cade dove cadeva.

- [ ] **Ascolto di entrambi in contesto**, con congas e shaker, sul brano vero.
  Le misure dicono che ora sono lo strumento giusto; non dicono se stanno bene
  nel mix.
- [ ] I tre `clap*.wav` sono ancora sul disco e nel binario (~90 KB): se il clap
  sintetico convince, si cancellano e si tolgono le righe da `ATTRIBUTION.md`.
- [ ] Nessun test copre il timbro delle voci. Il solo controllo automatico è la
  regressione d'attacco; il resto è `VPRender` più l'orecchio.

**Terzo caso, stessa forma (2026-09-09).** Segnalazione: *«sembra che sul 2 e sul
4 stia suonando un colpo differente»*. Era vero, ed era il **livello dinamico**.

`pick()` sceglie un layer con `int(velocity * 3)`, e per il cembalo ogni layer è
una *registrazione diversa* — `cembalo_down.wav` contro `cembalo_down_med.wav` —
più un filtro diverso in `layerFromRecording` (taglio a 12 kHz contro 7.5). La
velocity del cembalo è `shaker[step] * accent[beat]`, e attraversa il confine a
0.667 fra un quarto e l'altro:

| stile | beat 1 | beat 2 | beat 3 | beat 4 |
|---|---|---|---|---|
| marcha | 0.900 **L2** | 0.636 **L1** | 0.800 **L2** | 0.616 **L1** |
| dance | 0.700 **L2** | 0.551 **L1** | 0.698 **L2** | 0.513 **L1** |
| funk | 0.880 **L2** | 0.644 **L1** | 0.768 **L2** | 0.602 **L1** |
| samba | 0.648 L1 | 0.940 L2 | 0.598 L1 | 0.900 L2 |
| pop / bossa | L2 | L1 | L1 | L1 |
| rock | L2 | L2 | L2 | L2 (l'unico sano) |

Due timbri, agganciati al backbeat. E con `humanize` a ±7% i valori vicini al
confine (marcha 0.636, funk 0.644, samba 0.648) saltavano livello da un colpo
all'altro, quindi non erano nemmeno due in modo coerente.

**Correzione**: il cembalo non ha layer dinamici. Non è pigrizia sulle sue
dinamiche, è cosa è lo strumento — una pelle di conga colpita piano perde il
crack e risuona meno, un tamburello colpito piano è le stesse sonagliere che si
muovono meno, e l'unica differenza onesta è il livello. Una sola presa
(`cembalo_down.wav` / `cembalo_up.wav`) a forza piena per ogni layer e ogni
round-robin; gli accenti restano, perché `pick` copre già 0.08-1.00 di guadagno
in continuo con la velocity.

Questo chiude anche la stessa cosa che arrivava per un'altra strada: per
`ATTRIBUTION.md`, `cembalo_down_b` e `cembalo_up_med` sono un **secondo
tamburello** (VCSL Tamb1 contro Tamb2), quindi il round-robin cambiava strumento.
I file restano sul disco, semplicemente non vengono più letti per questa voce.

Misurato con `VPRender` (marcha, 100 BPM, `--humanize 0`, solo cembalo),
centroide spettrale dei colpi sugli ottavi:

```
          1      e      2      e      3      e      4      e
prima   9438   9710   9512   9704   9430   9711   9512   9851
dopo    9438   9877   9439   9887   9439   9879   9439   9891
```

Prima l'1 e il 3 leggevano ~9434 Hz e il 2 e il 4 leggevano 9512: campione
diverso. Dopo tutti i down leggono 9439 e tutti gli up ~9880 — restano due
timbri, che è giusto, perché down e up sono due articolazioni. Su rock, dance,
funk, samba e pop l'escursione fra i down è **0 Hz** e fra gli up ≤10 Hz. I
picchi (gli accenti) sono invariati riga per riga. `--swing` 3/0, `--bar` 10/0.

- [ ] **Da ascoltare.** La misura dice che i colpi sono lo stesso suono; non dice
  se il cembalo senza layer dinamici suona piatto in un crescendo vero. Se lo è,
  la strada non è rimettere i layer ma dare a `layerFromRecording` una curva di
  brillantezza continua per il metallo, invece di tre gradini.
- [ ] Lo **shaker** ha la stessa struttura (stesse tabelle, stessi accenti,
  stessi tre layer da tre file). Non è stato segnalato e non è stato toccato:
  va misurato allo stesso modo prima di decidere.

---

### 29. Su un intro senza batteria l'app si impegna su un tempo sbagliato e ci resta un minuto 🔴 (2026-09-09, misurato su brano reale — causa trovata, non corretta)

Segnalazione: *«spesso non riconosce il bpm del brano»*, *«va fuori e rientra
subito appena muovo leggermente il knob del mic»*, *«passa da 87 a 65 senza
senso»*. Brano di riferimento: `01 BLUE SKY.mp3` sul Desktop dell'utente,
convertito in `/tmp/vp-bluesky.wav` (l'originale non è toccato).

**Banco nuovo: `VPTrack`** (`scripts/probe_track.cpp`). Legge un wav e lo manda
nel motore **completo** — trim, canceller, guadagno d'analisi e
`updateAnalysisEpoch` — invece che nel solo `BeatTracker` come fa `VPLive`.
Serviva: su questo brano i due percorsi danno risposte diverse, e quello vero è
il peggiore.

```bash
cmake --build build-host --target VPTrack -j4
./build-host/VPTrack_artefacts/Release/VPTrack --wav /tmp/vp-bluesky.wav --bpm 87 --trace
```

**Il brano.** I primi trenta secondi non hanno né batteria né basso: energia
sotto i 200 Hz da 5.7 a 9.9, che sale a 37 e poi 79.7 fra i 30 e i 40 s quando
entra la sezione ritmica. Picco d'ingresso nell'intro: 0.013-0.020.

**Cosa fa il motore completo, a livello invariato:**

```
  t     pubbl   pettine  conf  gAnalisi
  4.0   52.49     0.00   0.52    7.54
 16.0   57.81   173.41   0.83    3.32
 20.0  171.29   181.82   0.10    3.40
 28.0  118.12   103.45   0.53    4.17
 40.0  163.01   170.70   0.68    1.92
 53.2  <- qui scatta il restart dell'analisi
 68.0   86.97    87.08   0.61    1.00
 88.0   86.38    86.58   1.00    1.00   FISSO
```

Primo aggancio tenuto 3 s: **66.1 s**. Dentro il ±2% solo il 45.6% dei primi
due minuti.

**Prima ipotesi, sbagliata e corretta subito.** Il guadagno d'analisi nei primi
50 secondi:

| livello | gAnalisi nei primi 50 s | primo aggancio | dentro il 2% |
|---|---|---|---|
| **0 dB** | 7.56 → 7.81 → 4.33 → 3.40 → 6.46 → 3.21 → 4.95 → 1.92 → 1.29 | **66.1 s** | 45.6% |
| −6 dB | 15.08 → … → 2.57 (saturato in alto) | **8.5 s** | 69.8% |
| +12 dB | 1.90 → 1.96 → 1.09 → 1.00 → 1.62 → 1.00 → 1.24 → 1.00 | **12.1 s** | 59.5% |

Sembrava la causa. **Non lo è**, e le due misure che l'hanno smontata:

1. A 0 dB e a −6 dB il livello **dopo** il guadagno è identico a tre decimali in
   ogni istante campionato (0.120 / 0.100 / 0.161 / 0.190 / …): il make-up
   compensa il trim esattamente come deve. Stesso segnale alla rete, e i due
   agganciano a 66.1 s e 8.5 s.
2. Sweep del tetto del guadagno, a livello invariato:

   | tetto | primo aggancio |
   |---|---|
   | 24× (attuale) | **66.1 s** |
   | 12× | 8.2 s |
   | 6× | 17.8 s |
   | **3×** | **66.1 s** — identico a 24×, a sei decimali |
   | 2× | 25.3 s |

   Nessuna monotonia. Non è un livello di soglia: è l'esito che cade in uno di
   pochi bacini, e 66.1 s è quello in cui finiscono impostazioni lontanissime.

**Quello che succede davvero.** Quattro corse identiche danno lo stesso numero a
sei decimali: il banco è deterministico. Ma la mappa fra configurazione ed esito
non lo è in alcun modo utile — **qualunque** perturbazione della catena d'analisi
fa ricadere il brano su un bacino diverso. Il knob non «sistema» niente: rimescola.

E il motivo per cui esistono bacini così diversi è a monte di tutto: **nei primi
trenta secondi non c'è una sezione ritmica**, la rete produce risposte
*confidenti e sbagliate* — 52, 57, 171, 120, 163, 133 — e l'app **si impegna su
una di quelle a 4 secondi**, con confidenza 0.52, e suona. Da lì in poi il minuto
successivo è deciso da quale sbagliata le è capitata.

**Il cancello: provato a costruirlo, e la misura dice dove non può stare.**

L'idea era: non impegnarsi finché non c'è pulsazione, usando la banda bassa come
segnale che una sezione ritmica non c'è. `lowBand` arriva già al decoder a ogni
frame (`observe(..., lowBand)`), quindi era costruibile. Misurato sul brano:

```
LB t=  4.02  medio=0.3818   <- intro senza batteria
LB t= 12.06  medio=0.6547
LB t= 20.10  medio=0.5177
LB t= 40.16  medio=0.7782   <- band entrata
LB t= 72.20  medio=0.7331
LB t= 96.32  medio=0.7879
```

**Non separa niente**: 0.65-0.70 nell'intro contro 0.73-0.79 dopo. Il motivo è
strutturale e sta scritto in `updateAnalysisEpoch`: la banda bassa è misurata
**dopo** il guadagno d'analisi, che nell'intro amplifica 7.5×, e a valle di quel
guadagno «an empty room and a band playing arrive looking alike - by design».

Quindi un cancello basato su qualunque cosa misurata dopo il make-up **non può
vedere** che manca la sezione ritmica. Deve stare prima, cioè dove vive già
`updateAnalysisEpoch` — l'unico punto in cui la differenza esiste ancora.

- [x] **Il cancello va costruito nel motore, prima del guadagno d'analisi**, non
  nel decoder. Lì `rawPeak` e la banda bassa del segnale non amplificato dicono
  ancora se c'è una sezione ritmica. È lo stesso posto che già decide l'epoch, e
  probabilmente le due cose sono lo stesso lavoro.
- [ ] Un cancello basato solo sulla **stabilità del tempo pubblicato** non basta,
  e la traccia lo dimostra: il decoder resta fermo su 118 BPM per otto secondi
  (t=28-36) e su 133 per quattro. È confidente, stabile e sbagliato.
- [x] L'epoch scatta a **53.2 s**, venti secondi dopo l'ingresso della band
  (30-40 s), perché il riferimento era già stato trascinato in alto dall'intro
  amplificata. Da rivedere insieme al punto sopra.
- [ ] **Non toccare il guadagno d'analisi** sulla base di questo brano: è
  misurato che non è la causa, e le sue costanti sono tarate altrove.

**COSTRUITO (2026-09-09): la quota di banda bassa, prima del make-up.**

`VirtualPercussionEngine::updateRhythmShare` misura, sul segnale d'analisi non
amplificato, quanta dell'energia sta sotto i 200 Hz — un **rapporto**, non un
livello. È l'unica statistica misurata su questo brano che separa l'intro dalla
band, e sopravvive al make-up perché un guadagno a banda larga non può falsare
un rapporto. Numeri sul brano, dal motore stesso (colonna `lowS` di `VPTrack`):
intro 0.10-0.28, band 0.32-0.50.

Due fatti diversi ne escono, e servono a due cose diverse:

- **Il gradino** (share > 2× l'altopiano in cui stava, tenuto 1.5 s, sopra 0.30):
  «è appena entrata una sezione ritmica». Fa scattare l'epoch, con la stessa
  uscita del gradino di livello. Sul brano scatta a **39.7 s** invece di 53.2.
- **`rhythmSeen`** (share sopra 0.30 per 2 s, o il gradino): «adesso ce n'è una».
  Ora è richiesto, insieme al livello, da `tracker.setSourceAudible` — cioè
  dall'unico percorso che lascia entrare la parte su musica già in corso
  (`alreadyPlaying`). Una sorgente che parte dal silenzio entra dal suo epoch e
  non è toccata da questo.

| | prima | dopo |
|---|---|---|
| primo aggancio tenuto 3 s | 66.1 s | **55.0 s** |
| dentro il ±2% (brano intero) | 67.5% | **71.8%** |
| epoch | 53.2 s | **39.7 s** |
| suona durante l'intro | sì, da 4.0 s a 52 BPM | **no, mai** |

È relativo per costruzione, ed è questo che lo rende sicuro altrove: il kit
sintetico dei banchi sta piatto a 0.17 per tutta la sua durata e il click a
0.002, quindi nessuno dei due può gradinare, e tutti e due entrano dal loro
epoch di livello (`quietLeadIn`).

**La costante che è costata la prima versione.** Il primo taglio del cancello
d'ingresso ha portato `--level` da 15/1 a **13/3** — proprio la colonna
«ingresso». Non era la soglia: era che `rhythmSeen` non poteva essere vero prima
di 4 s (2 s di prime più 2 s di tenuta) mentre sui banchi la parte entra **0.41 s
dopo l'inizio della musica**. Le due energie ora sono innescate al primo blocco
invece di essere rilasciate, e la tenuta è 0.33 s. I numeri sul brano non
cambiano di un decimale e `--level` torna a 15/1. Chi tocca queste costanti
rimisuri quella colonna: è l'unica che le vede.

Regressioni, tutte rimisurate sulla versione finale: `--level` **15/1** (stesso
unico FAIL, 91 BPM a 0 dB), `--octave` **7/4**, `--bar` **10/0**,
`--tempo-slow` **10/0**, `probe_matrix` **identico** (99 uscite, aggancio medio
5.27 s), `probe_tempo_step` invariato, `VPAlign` **identico riga per riga**
contro un binario ricostruito dai sorgenti puliti. `probe_matrix` e
`probe_tempo_step` non compilano nemmeno il motore, quindi non potevano
cambiare; `VPAlign` sì, ed è stato confrontato davvero.

**L'arbitro pettine/rete, nel decoder (stessa data).** Idea dell'utente: «il
tempo fra due colpi». Un arbitro c'era già — lo snap d'ottava e il watchdog
della griglia stantia — ma non poteva parlare in tempo, e la traccia a 1 s dice
esattamente perché:

```
  39.7  epoch: il fold viene azzerato
  40-47 il pettine non è pronto (0.00) — non c'è niente da arbitrare
  47.0  pettine pronto: 89.69, poi 87.72 / 87.85 / 87.98
  49.0  levelSettled diventa 1
  53.0  snap: pubblicato 57.5 -> 87.8
```

`combMayCorrect` aspettava `tempo.levelSettled()`, e **ogni ragione per
aspettarlo è una ragione d'ottava**: senza, il pettine potrebbe star nominando
il doppio. Non dice niente su due letture distanti una quinta (55.9 contro
87.7) — su qualunque ottava sia il pettine, la griglia impegnata non è su
nessuna delle due. Quindi un disaccordo **non d'ottava**, con pettine saliente,
non aspetta più. Snap a 49.0 invece che 53.0.

Solo contro un livello **provvisorio**, e quel vincolo è misurato, non
supposto. Senza, la stessa rilassatezza arriva a una griglia stabilita e
`VPAlign` dice quanto costa: 132 BPM con 2.2 ms di jitter passa da rms 0.07
battiti a 0.23 con un peggiore di mezzo battito — il livello sbagliato, preso
con l'argomento che questo doveva risolvere — e la rampa 100→110 in 12 s perde
5 ms di fase del decoder. Con il vincolo, entrambe tornano identiche alla base.

| | senza arbitro | con arbitro |
|---|---|---|
| primo aggancio tenuto 3 s | 55.0 s | **53.5 s** |
| dentro il ±2% | 71.8% | **73.6%** |
| snap dopo il restart | 53.0 s | **49.0 s** |
| `probe_matrix` uscite | 99 | **96** |
| `probe_matrix` aggancio medio | 5.27 s | **5.25 s** |
| `probe_matrix` fuori medio | 9.00% | **8.99%** |

`probe_matrix` **non è identico**, ed è la prima volta che questo cambio lo
tocca davvero: i tre totali migliorano tutti, ma per materiale il quadro è
misto — `swing 8vi` 3.91 → 3.67 e peggiore 11.60 → 9.56, `mix che ingoia`
8.62 → 8.04 con uscite 16 → 14, contro `swing pieno` 4.31 → 4.83 (ma uscite
2 → 1 ed errore 0.07% → 0.01%) e `solo accordi` 4.86 → 4.90. `probe_tempo_step`
identico. `VPAlign` differisce ancora su due righe: il 168 a 2.2 di jitter, che
è **già annotato «livello sbagliato» nella base** (rms 0.2691 → 0.2730, scatti
8 → 12, ma peggiore 0.4402 → 0.3732), e frazioni di millisecondo sulla rampa
100→110 in 30 s. `--level` 15/1, `--octave` 7/4, `--bar` 10/0, `--tempo-slow`
10/0.

- [ ] **Continuità dell'analisi all'ingresso della band (prova 09/09).**
  Distinto l'evento di quota bassa dal normale quiet-to-loud: il primo conserva
  pettine e stato ricorrente della rete, azzerando la griglia del decoder;
  il secondo continua a scartare tutta l'evidenza. Evento e tipo viaggiano
  insieme nello stesso valore atomico verso il worker.
  Su BLUE SKY, a 0 dB: primo aggancio tenuto tre secondi **53.46 -> 41.285805 s**,
  quota entro ±2% sul brano intero **73.6 -> 73.8%**. Epoch sempre 39.7 s.
  Conservare il solo pettine, resettando la rete, peggiora a **56.971610 s**:
  non basta evitare il riempimento del buffer. La continuità della rete conta.
  Il miglioramento non equivale a stabilità perfetta: fra 47 e 55 s la variante
  continua sale intorno a 90 BPM; inoltre suona già a 40 s mentre il clock
  sta raggiungendo il nuovo tempo. Questi due limiti restano aperti.
  `probe_input_continuity` verifica conservazione e successivo reset completo
  a 52/87/120/168 BPM (8 controlli). Non misura il modello o il suono.

- [ ] **Diagnosi precedente dei 7 s di riempimento**: fra l'epoch (39.7) e il
  momento in cui il pettine è pronto (47.0) non esiste un secondo parere. È il
  riscaldamento del fold dopo che `notifyInputRestart` lo ha azzerato. Da notare
  che a 39.0 s, *prima* del restart, il pettine leggeva già **85.71** con
  `levelSettled` a 1: l'epoch butta via una risposta che era già giusta. Un
  restart che distingua «stanza → band» (dove l'evidenza è davvero della stanza)
  da «è entrata la sezione ritmica» (dove gli ultimi secondi contengono già la
  band) recupererebbe quei 7 s. È il pezzo più grosso che resta su questo brano.
- [ ] Lo stato pubblicato resta `following` durante l'intro (su un BPM sbagliato)
  anche se la parte tace. Il display mente ancora; solo il suono no.

**Provato e scartato (misurato due volte, tenuto in scratch):** un watchdog che
si accorge quando la griglia «muore di fame» — accetta meno del 55% dei battiti
che il suo stesso tempo prevede mentre gli eventi continuano ad arrivare. Scatta
correttamente e porta `90 → 120` da 20.7 a 10.2 s sul banco sintetico, ma sul
brano vero scambia il crollo a 53 BPM con uno a 131 e arriva **allo stesso
secondo**. La variante che adotta il pettine invece di ripartire non scatta, e
giustamente: nei primi 25 s di questo brano il pettine salta 84 → 173 → 87 →
110 → 0, non c'è un secondo parere da prendere.

---

### 30. A volte all'avvio non si sente niente, e serve cambiare il clock e rimetterlo 🟡 (2026-09-09, causa trovata nel codice — non riprodotta a mano)

Segnalazione: *«a volte l'audio non si sente e devo cambiare da "auto" a 44.100
per esempio, e poi rimettere "auto"»*.

**Cosa fa davvero quel gesto.** Su desktop `deviceSampleRate()` in AUTO
restituisce `vp::sessionSampleRate()`, che fuori da iOS è 0, e uno 0 viaggia
fino in fondo come «non scrivere nessuna frequenza». Quindi AUTO e 44100 non
sono due frequenze diverse: AUTO lascia il setup com'è. L'unica cosa che quel
gesto fa è **riaprire il dispositivo**. La cura non è la frequenza, è la
riapertura — ed è questa l'informazione che indica dove guardare.

**Il buco.** Il watchdog del timer aveva questo ramo:

```cpp
else if (haveDevice && ! audioReady)
{
    // Between close and prepareToPlay. Not a stall.
    stalledTicks = 0;
}
```

Senza limite. E quello è uno stato in cui l'app può restare per sempre:
`AudioSourcePlayer` di JUCE chiama `getNextAudioBlock` dal callback del
dispositivo indipendentemente dal fatto che `audioDeviceAboutToStart` sia
passato. Se non è passato, `inputScratch` è ancora il buffer costruito di
default — **zero canali, zero campioni** — quindi in `getNextAudioBlock`
`count`, `nCopy` e `used` sono tutti zero, `engine.process` non scrive niente e
il blocco finisce nel `buffer->clear` in fondo. Silenzio, mentre `audioBlocks`
continua a contare. Il dispositivo sembra vivo, al watchdog viene detto che non
è uno stallo, e non si sente niente finché non lo si riapre a mano.

**Correzione**: lo stesso limite degli altri rami, dodici tick a 15 Hz (~0.8 s).
Una chiusura-e-riapertura vera dura un tick o due, perché l'avvio è sincrono;
oltre il secondo non è una transizione, è un dispositivo che non è mai partito.
Passato il limite l'app fa da sola la riapertura che l'utente faceva a mano, con
lo stesso raffreddamento di 2 s degli altri due casi.

E il motivo dell'ultima ricostruzione ora è **sulla pagina diagnostica** accanto
al contatore: `riavvii 1 device open but never prepared` è una cosa diversa da
`riavvii 1 no audio callback for a second`, e sono tre rig diversi.

- [ ] **Non riprodotto a mano.** È intermittente e dipende dall'ordine di
  apertura del dispositivo; quello che c'è è che questo è l'unico percorso nel
  codice che produce esattamente quel sintomo (dispositivo aperto, callback che
  girano, silenzio, e la riapertura come unica via d'uscita). La verifica è la
  riga `riavvii` sulla pagina diagnostica: se il difetto si ripresenta e adesso
  si risolve da solo dopo un secondo, quella riga lo dirà.
- [ ] Su un'**interruzione iOS** (una telefonata) il dispositivo può restare
  aperto e fermo. Adesso dopo 0.8 s si ricostruisce, cosa che durante la
  chiamata fallisce e riprova ogni 2 s, e al termine rientra da sola. È lo
  stesso comportamento che il ramo «nessun dispositivo» ha già oggi; se in
  pratica dà fastidio, il ramo va sospeso mentre `appIsSuspended` è vero.
- [ ] **AUTO su desktop non è «segui l'hardware», è «lascia com'è».** Fuori da
  iOS `sessionSampleRate()` è 0, quindi dopo aver scelto 44100 e rimesso AUTO il
  dispositivo resta a 44100. Il commento in `deviceSampleRate()` dice perché
  scrivere una frequenza è caro (ri-clocca un'interfaccia che tutta la sala sta
  ascoltando), quindi non è stato toccato — ma l'etichetta mente un po'.

---

### 31. Ogni tanto deriva e ci mette troppo a rientrare 🟡 (2026-09-09, misurato e dimezzato — non ancora dentro la battuta ovunque)

Segnalazione: *«ogni tanto deriva troppo e ci mette tanto a riprendere (anche 5
secondi — deve rientrare in una battuta)»*, con la domanda giusta attaccata:
*«ogni battuta, o ogni tot secondi o millisecondi?»*.

**La risposta alla domanda è la parte più utile di questo item.** Il controllo
esiste già ed è **per battito**. Il problema non è la cadenza: è che due dei
budget sono espressi in battiti, e un battito è un secondo a 60 BPM e 0.375 s a
160. Lo stesso numero è una scadenza diversa a ogni tempo, e la scadenza che
l'ascoltatore sente è la **battuta**.

**Dove vanno i secondi**, `probe_steady_tempo` (tempo costante, deriva 3 BPM,
jitter 10 ms, 11 tempi × 10 semi × 300 s). Sette uscite su 110 corse, e sono due
difetti diversi:

| BPM | battuta | quanto | letto | pettine | fit corto | fit lungo |
|---|---|---|---|---|---|---|
| 60 | 4.00 s | **6.5 / 1.4 / 6.7 s** | 61.08 | 59.70 | 59.67 | 60.87 |
| 150 | 1.60 s | **2.0 s** | 142.90 | 150.38 | **0** | **0** |
| 160 | 1.50 s | **1.9 / 2.2 s** | 152.06 | 159.15 | **0** | **0** |
| 170 | 1.41 s | **1.8 s** | 178.53 | 170.94 | **0** | **0** |

- **Ai tempi veloci** l'uscita segue una transizione che ha **azzerato i fit** e
  pubblicato un numero sbagliato. Il pettine ha ragione per tutta la durata. Il
  numero rientra a 0.7 BPM per battito: `pullTowardsComb` dà al pettine il 35%
  di autorità e `kRateLive` ne applica il 22% per battito — **7.7% effettivo,
  cioè 13 battiti (tre battute) per chiudere**.
- **A 60 BPM** il regime è **FISSO**, e in FISSO il pettine non era consultato
  affatto: `live` e `unknown` passano il loro bersaglio per `pullTowardsComb`,
  quel ramo seguiva `fixedAnchorBpm` e nient'altro. Traccia per battito:

```
  26.76  read=61.08  short=59.48  comb=59.70  FISSO   <- congelato
  27.78  read=61.08  short=59.32  comb=59.46  FISSO
  28.82  read=61.01  short=59.10  comb=59.29  FISSO
  30.86  read=59.84  short=58.72  comb=58.82  LIVE    <- esce solo qui
```

**Due correzioni, entrambe misurate.**

1. **Il pettine entra in FISSO**, limitato bene dentro l'ottava. Non
   `pullTowardsComb` così com'è: il suo tetto è `kOctaveThreshold`, quindi tira
   anche fra l'8 e il 19%, che è dove vive un argomento sul livello metrico e di
   cui lo snap è padrone. Sotto `kStaleGridThreshold` non c'è nessun livello da
   confondere, e il caso a 60 BPM sta al 2-3%. Sotto il 3% non fa niente, quindi
   un disco tagliato a click non viene toccato.
2. **La battuta dopo uno stato stantio si spende a rincorrere**, non a inclinare:
   uscendo da FISSO (il numero tenuto è stale per definizione, è il motivo per
   cui si esce) e mentre `transitionRefitBeats` dice che i fit si stanno
   ricostruendo dopo una transizione. **Non cambia dove va il tempo, solo quanto
   ci mette**: il bersaglio è lo stesso fit ricostruito in entrambi i casi, ed è
   questo che la rende sicura su un gradino vero. Con la guardia del 2%, perché
   senza scattava anche quando il numero era già giusto.

| BPM | battuta | prima | dopo |
|---|---|---|---|
| 60 | 4.00 s | 6.5 / 1.4 / 6.7 | **4.4 / 1.4 / 4.7** |
| 150 | 1.60 s | 2.0 | **0.8** |
| 160 | 1.50 s | 1.9 / 2.2 | **1.1 / 1.8** |
| 170 | 1.41 s | 1.8 | **0.7** |

Dentro la battuta: **1 uscita su 8 prima, 4 su 8 dopo**; la peggiore da 6.7 a
4.7 s. Errore medio del banco migliorato a 8 tempi su 11 e peggiorato a nessuno.

**Tre ipotesi morte alla misura, e vanno lette prima di ritentarle.**

1. **Il pettine come test d'uscita da FISSO.** `anchorError` confronta il fit
   lungo con una media corrente del fit lungo: entrambi i lati vengono dal
   numero che il tempo tenuto produce, quindi un fit lungo in ritardo alimenta
   l'anchor *e* lo certifica. La riparazione ovvia è chiedere al pettine, che è
   fuori da quel ciclo. Funziona sul caso per cui è scritta e **rompe un gradino
   vero**: sul 120 → 160 non protetto di `probe_tempo_step` il pettine nomina
   120 per secondi dopo il cambio, il test legge 25% dalla parte sbagliata, il
   regime viene rilasciato con il pettine stantio come voce più forte, e la
   corsa **collassa a 53.3 BPM e non torna più**, contro 23.6 s per arrivare al
   tempo giusto. Sospenderlo durante una transizione confermata non lo salva: il
   gradino non protetto non ha nessuna transizione da sospendere. È lo stesso
   muro che la skill registra per il gate delle transizioni.
2. **Accorciare il budget d'uscita da FISSO** (`kBeatsToLeaveFixed`, 6 → 4 → 3).
   Durate identiche al decimo di secondo in tutte e tre. L'attesa non è il
   contatore: è il fit lungo che arriva alla linea del 2%, e la sua finestra a
   tempo lento è lunga nove secondi. Ripristinato a 6.
3. **Il fit corto nel test d'uscita.** Era già 2.7% sotto al primo battito. Non
   provato fino in fondo perché la misura che c'è già lo esclude: la skill
   riporta che il movimento battito-per-battito del fit corto è 0.5-1.15 BPM a
   tempo fermo, che a 60 BPM è fino all'1.9% — contro una soglia del 2%.

**Cosa costa, e va deciso dall'utente.** La rincorsa (punto 2) è la metà cara:

- `probe_matrix`: uscite **96 → 98**, tutte e due su `band larga` (banda
  sfilacciata, 25 ms di scatter). In compenso `fuori medio` 8.99% → **8.98%** e
  cinque materiali migliorano la loro percentuale fuori tempo — metronomo
  0.05 → 0.02, backbeat secco 0.42 → 0.30, shuffle 16mi 0.05 → 0.02 — con i
  rapporti più vicini a 1.00. Aggancio medio invariato a 5.25 s.
- `VPAlign`: due righe **migliorano** (a t=32 il tempo passa da 117.91 a 119.32
  contro un vero 120, con la fase da 12.8 a 7.3 ms; `fase media` 14.8 → 14.4) e
  sette righe della media su otto brani **peggiorano di 0.5-3 ms** attraverso il
  buco senza batteria e l'accelerando, su numeri che stanno già a 20-40 ms.
- Invariati: `probe_tempo_step` identico riga per riga, `--level` 15/1,
  `--octave` 6/5, `--bar` 10/0, `--tempo-slow` 10/0, `--swing` 3/0.

Isolato per bisezione: il punto 1 da solo non costa niente (`probe_matrix` 96 e
due righe migliori, `VPAlign` solo le due righe migliori). Tutto il costo è la
rincorsa, ed è la rincorsa a sistemare i tempi veloci. Per toglierla basta
`const bool far = false && ...` in `updateTempo`.

- [ ] **Restano fuori 3 uscite su 8.** A 60 BPM 4.4 e 4.7 s contro 4.0, e a
  160 BPM una da 1.8 s contro 1.50. Quella a 160 è istruttiva: fra due battiti
  accettati passano **1.48 s** — quattro battiti che la griglia rifiuta dopo la
  transizione. Un controllo per battito non può fare niente quando i battiti non
  arrivano, ed è lì che serve la seconda metà della domanda dell'utente: un
  fondo a tempo d'orologio, non a battiti.
- [ ] **A 60 BPM il vincolo è il ritardo del fit lungo**, non il budget. Nove
  secondi di finestra su materiale che deriva, e nessuna delle tre sorgenti
  legge il vero (58.73): il fit lungo dice 60.87, il pettine 59.70, il corto
  59.48. La strada è probabilmente non entrare in FISSO su materiale che deriva,
  cioè `mayFix`, e ha un raggio d'azione molto più largo di questo item.
- [ ] `--octave` è **6/5 su questo HEAD**, non 7/4: è cambiato fuori da questo
  lavoro (le modifiche a `NeuralBeatTracker`/`BeatTracker` di un'altra sessione).
  Ri-baselinare prima di attribuirlo a un cambio del decoder.

---

### 32. «Sempre in ritardo della stessa quantità, e ci mette molti secondi a rientrare» 🟡 (2026-09-10, causa trovata e metà corretta)

Segnalazione, ed è la descrizione che ha risolto il caso: *«sono sempre in
ritardo (o sempre in anticipo) e ci impiegano molti secondi a rientrare»*.

Le due metà di quella frase sono **una sola costante**. `TempoFollower` aveva un
pavimento di fase di `0.012` **di battito**, e una frazione di battito è un
numero di millisecondi diverso a ogni tempo:

| BPM | 52 | 60 | 75 | 90 | 120 | 168 |
|---|---|---|---|---|---|---|
| banda morta | **13.8 ms** | 12.0 | 9.6 | 8.0 | 6.0 | 4.3 |

Il bersaglio dichiarato per dove atterra la parte è **8 ms**, quindi sotto i
90 BPM la banda morta era **più larga della cosa che deve ottenere**. E viene
*sottratta* dall'errore, non confrontata:

```cpp
const float e = phaseErrEma > kPhaseFloor ? phaseErrEma - kPhaseFloor : ...
```

quindi la correzione svanisce mano a mano che l'errore si avvicina al pavimento.
Il residuo non si chiude: **converge al pavimento e resta lì**. «Molti secondi a
rientrare» è l'avvicinamento asintotico; «sempre in ritardo della stessa
quantità» è dove si ferma.

`probe_recovery --slow-passages` lo diceva già, ed era annotato come aperto:
tutti e 18 i casi a 52 BPM leggevano `confirm=-1.000 stable=-1.000` — mai
confermato e mai dentro gli 8 ms.

**Correzione: il pavimento è limitato nel tempo, non in battiti.** Quello da cui
protegge è il rumore di fase dell'*analisi*, che è un tempo e non una frazione
di battito: viene da una griglia di frame da 20 ms, che è 20 ms a ogni tempo.

```cpp
constexpr float kPhaseFloorBeats = 0.012f;
constexpr float kPhaseFloorSeconds = 0.006f;
inline float phaseFloorFor (float periodSec)
{ return std::min (kPhaseFloorBeats, kPhaseFloorSeconds / std::max (0.05f, periodSec)); }
```

6 ms **è** 0.012 di battito esattamente a 120 BPM, cioè dove la costante
originale era stata tarata: quindi **da 120 BPM in su non cambia nulla**, il
valore in battiti è già il più piccolo dei due. Impedisce solo che il pavimento
si allarghi sotto quel tempo.

| | prima | dopo |
|---|---|---|
| `probe_recovery` default (120/168) | 0 FAIL / 84 PASS | **identico** |
| `--slow-passages`, tutti e 18 i casi a 52 BPM | `stable=-1.000` (mai) | **3.3-4.4 s** |
| `VPAlign`, aggancio a 70 BPM | 3.61 s | **3.43 s** |
| `VPAlign`, aggancio a 100 BPM | 3.14 s | **3.05 s** |
| `VPAlign`, dieci righe di rallentando | — | **~0.2 s meglio ognuna** |

Le uniche righe peggiori di `VPAlign` si muovono di **0.1 ms** (39.8 → 39.9), che
è rumore numerico. `--level` 15/1, `--octave` 6/5, `--bar` 10/0, `--tempo-slow`
10/0, `--swing` 3/0, tutte alla base. `probe_matrix` e `probe_tempo_step` non
compilano `TempoFollower` e non possono cambiare.

**Provato e scartato nella stessa ora.** Dare lo stesso trattamento all'altro
pavimento, `persistentFloor = max(0.04f, 0.020f/period)` — che a 52 BPM vale
46 ms — scalandolo con il pavimento tempo-consapevole. Aritmeticamente è lo
stesso numero a 120 BPM, ma **`probe_recovery` è passato da 0 a 5 FAIL sul gate
di default** e l'estensione lenta da 18 a 23. I due pavimenti non sono la stessa
manopola: questo arma una correzione *rapida*, e una soglia più bassa la arma su
evidenza che non si è ancora assestata. Lasciarlo stare.

- [ ] **Resta `confirm=-1.000` a 52 BPM**: il percorso di recupero rapido non si
  conferma mai lì, ed è il motivo per cui i 18 casi restano FAIL nonostante
  `stable` sia passato da mai a 3.4 s. È quel `persistentFloor` da 46 ms, e la
  prova qui sopra dice che non si tocca da solo.
- [ ] **3.4 s è ancora tanto** contro una battuta di 4.6 s a 52 BPM. Meglio di
  «mai», non ancora «subito».
- [ ] Da riascoltare sul materiale dell'utente: la misura dice che il residuo
  permanente non c'è più, non dice che l'orecchio sia contento.

---

### 33. Il batterista suona solo charleston in quarti 🟢 (2026-09-10, misurato: il tempo è perfetto, il problema è l'uno)

Segnalazione: *«a volte il batterista suona solo il charleston in quarti. L'app
ha difficoltà a capire e seguire il tempo.»*

**Misurato, e il tempo non è il problema.** Charleston chiuso in quarti, niente
cassa, niente rullante, tre secondi di silenzio prima come in un brano vero:

| BPM | primo aggancio | dentro il ±2% | ottava |
|---|---|---|---|
| 60 | 6.23 s | **100.0%** | giusta |
| 76 | 6.23 s | **99.1%** | giusta |
| 96 | 2.56 s | **99.3%** | giusta |
| 132 | 4.77 s | **99.7%** | giusta |

Confidenza 1.00, pettine esatto a 96.00, residuo 0.002, copertura 1.00. Su
questo materiale il tempo è tenuto meglio che su quasi tutto il resto del banco.

**Il problema è la battuta.** Colonna `1?` nuova nella traccia di `VPTrack`
(`barTrusted`): **`no` per tutta la durata**, e resta `no` anche con un accento
sull'uno. È il comportamento *giusto* — un charleston piatto non contiene
nessuna informazione sul downbeat, e la rete lo sa — ma le conseguenze si
sentono:

- il **clap** correttamente non entra (è cancellato da `barTrusted`);
- la figura delle **congas** deve comunque scegliere un uno, e su un charleston
  piatto è testa o croce.

Il risultato all'orecchio è: la pulsazione è giusta, gli accenti sono nel posto
sbagliato. Per un musicista quello suona come «non ha capito il pezzo», anche
se il BPM è esatto al centesimo. **È quasi certamente questo che l'utente
sente**, non un errore di tempo.

La risposta è la stessa dell'ottava: quell'informazione non è nell'audio, e
l'app ha già il comando per riceverla dall'esterno — **TAP / SPOSTA L'1**. Su
materiale che non porta il downbeat non è un ripiego, è l'unica sorgente
possibile.

- [ ] **Da verificare all'orecchio**: quando succede, la pulsazione è giusta e
  l'accento è spostato? Se sì è questo item e la cura è il tap. Se il polso
  stesso vacilla, è un altro problema e serve una registrazione.
- [ ] **Buco stretto introdotto dall'item 29**: `rhythmSeen` si aggancia sulla
  quota di banda bassa, e un charleston ne ha **zero** (`lowS = 0.000` misurato).
  Un brano che parte dal silenzio entra comunque dal suo epoch — verificato, la
  parte suona — ma se si preme START **durante** un passaggio di solo charleston,
  senza nessun momento di quiete prima, `alreadyPlaying` non si apre e la parte
  non entra finché non arriva qualcosa con del basso. Il TAP la libera.
  La riparazione onesta sarebbe un secondo modo di agganciare `rhythmSeen` — per
  esempio una griglia pulita e stabile su cui pettine e rete concordano, che
  l'intro di BLUE SKY non produce mai — ma va misurata contro `probe_room` e
  `VPTests --makeup` prima, perché la stanza vuota raggiunge FOLLOWING a 0.91 di
  confidenza e non deve poter aprire questo cancello.

**Nota sulla priorità del modello.** Questo caso è materiale *senza melodia e
senza basso*, ed è proprio quello per cui si sarebbe detto «serve un modello
diverso». Non serve: la rete lo tiene al centesimo. Vale come dato quando si
rivaluta quanto in alto sta davvero il modello nella lista.

---

### 34. Prima misura su una band vera: `Flamingo Marco 09.07.26` 🔴 (2026-09-10, misurato, non corretto)

Registrazione di una serata intera, **97 minuti, mandata del banco** — cioè lo
stesso tipo di segnale che l'app riceverà sul palco. È il primo materiale vero
che questo progetto abbia mai misurato. Segmentazione dai livelli e dal tempo:

| # | dal | al | tempo |
|---|---|---|---|
| 1 | 0:10 | 4:40 | ~82 (l'utente ci mette lo SWING) |
| 2 | 5:10 | 9:30 | ~72 / 107 |
| 3 | 10:10 | 14:50 | ~104 — *Sally*, segnalato come il peggiore |
| 4 | 15:10 | 19:40 | ~86 |
| 5 | 20:20 | 24:20 | ~89 |
| 6 | 25:30 | 27:50 | ~68 / 103 |

#### Brano 3 (Sally): l'app oscilla il 40% più del batterista

Stima indipendente del tempo vero con un tempogramma a finestra di 12 s
(autocorrelazione del flusso spettrale, solo i punti con nitidezza > 0.15).
**Non è verità di fase** — è un secondo parere — ma basta per il tempo:

| | |
|---|---|
| il batterista | 101.4-107.2 BPM, dev.std **1.6%** |
| l'app | 95.4-109.0 BPM, dev.std **2.2%** |
| errore medio dell'app | **1.34%**, peggiore **7.78%** |
| errore medio del **pettine** | **0.96%** |
| uscite oltre il 4% | 2, di cui una da **10 secondi** |

Il batterista oscilla davvero — è esattamente «un batterista non professionista
senza clic» — ma l'app amplifica. E **il pettine è più vicino al vero del numero
che l'app pubblica**: una delle sue sorgenti batte il suo risultato.

L'uscita da 10 secondi, per battito:

```
 18.0  pubbl 103.43   rete 103.43   pettine 102.92   cop 1.00
 20.0  pubbl  95.64   rete  95.64   pettine 103.27   cop 1.00   <- la rete crolla
 22.0  pubbl  95.57   rete  95.50   pettine 102.92   cop 0.80
 26.0  pubbl  95.37   rete  95.63   pettine 102.21   cop 0.62
 28.0  pubbl  95.59   rete  95.60   pettine 102.56   cop 0.00
 30.0  pubbl 101.38   rete 101.47   pettine 104.35   cop 0.00
 34.0  pubbl 102.47   rete 102.45   pettine 103.63   cop 1.00
```

La rete scende a 95.6 e ci resta dieci secondi; **il pettine dice 102-104 per
tutti e dieci**, e la copertura della griglia crolla a zero — la griglia a 95.6
sta rifiutando i battiti veri. Il disaccordo è `log2(103.27/95.64) = 0.111`,
cioè l'**8.0%**: sopra `kCombPullThreshold` (3%) e **sotto**
`kStaleGridThreshold` (8.7%). Nessuno dei due meccanismi decisivi lo possiede;
agisce solo la trazione lenta verso il pettine, 35% di autorità × 22% per
battito = **7.7% effettivo, tredici battiti, tre battute**. Che sono i dieci
secondi.

#### Due tentativi, entrambi misurati e scartati

**1. Dare più autorità al pettine (`kCombPull`).** Non è monotono:

| `kCombPull` | uscite | durate | totale fuori | errore medio |
|---|---|---|---|---|
| **0.35** (base) | 2 | 10 / 2 s | 12 s | 1.34% |
| 0.45 | 4 | 22 / 4 / 6 / 2 s | **34 s** | 1.64% |
| 0.55 | 3 | 6 / 6 / 2 s | 14 s | 1.30% |
| 0.65 | 3 | 10 / 12 / 2 s | 24 s | 1.31% |

È lo stesso fenomeno a bacini già documentato nell'item 29 per il guadagno
d'analisi. **Su un brano solo una costante non si tara: si fitta il rumore.**
Ripristinato a 0.35.

**2. Abbassare `kStaleGridThreshold`** da 0.120 a 0.070, per coprire la banda
5-8.7% in cui questo caso cade. `probe_matrix` **identico** (98 uscite, 5.25 s,
8.98%) — su quel banco la banda non scatta mai — ma anche la traccia di Sally è
**identica byte per byte**: il watchdog non arriva comunque a votare, e il
motivo non è la soglia. Ripristinato a 0.120.

#### Brano 1 (swing): ottimo dentro, difficile all'inizio

Da 30 s a 270 s tiene **82 BPM** con confidenza fino a 1.00, copertura 1.00 e
`barTrusted` = SI. Il problema sono i primi venti secondi:

```
   2.0  gAnalisi 23.99   picco 0.001            <- guadagno al tetto, ingresso muto
   6.0  pubbl 105.27     picco 0.000            <- pubblica 105 sul nulla
  16.0  pubbl 116.01     picco 0.050  suona SI  <- entra la band, e la parte entra su 116
  16.7  epoch
  20.0  pubbl  81.53                            <- giusto
```

Sedici secondi di ingresso praticamente muto (`picco` 0.000-0.001), il make-up
al tetto di 24×, e la rete che risponde al rumore amplificato con 105 BPM a
confidenza 0.62. Poi **quattro secondi di parte suonata su 116 BPM** prima che
l'epoch la corregga.

- [ ] **Difetto del cancello dell'item 29, misurato qui.** Con il guadagno a 24×
  su un ingresso muto, `lowS` legge **0.34-0.46** — cioè la quota di banda bassa
  del *rumore amplificato*, che è alta perché il rumore è tutto in basso. Quindi
  `rhythmSeen` si aggancia sul silenzio. Qui non è stato lui a far entrare la
  parte (l'ha tenuta fuori il test di livello, `picco` 0.001 < 0.040), ma è un
  aggancio falso che va tolto: `updateRhythmShare` deve rifiutare di votare
  quando l'energia pre-make-up è sotto il pavimento dell'udibile.
- [ ] **I quattro secondi su 116 BPM** non li avrebbe evitati: dopo l'epoch il
  cancello è aperto comunque da `sawInputStart`, e il decoder ha bisogno di quei
  secondi per riacquisire. È un problema diverso.

#### Cosa serve adesso, ed è la conclusione operativa

L'utente chiede: *«se succedono live troppe uscite crea difficoltà; dovrebbe
almeno essere velocissimo a riprendersi»*. La velocità di rientro **è** quella
trazione da 7.7% per battito, e la misura qui sopra dice che non si può tarare
su un brano.

- [ ] **Costruire un banco dai sei brani di questa registrazione**, con la stima
  indipendente del tempo per ciascuno, e tarare contro quello. È la prima volta
  che c'è materiale per farlo. `probe_matrix` resta il banco sintetico; questo
  diventa il banco reale, e i due vanno guardati insieme.
- [ ] Il tempogramma indipendente (`scratch/tempocurve.py`) va portato in
  `scripts/` e reso ripetibile, altrimenti il banco non esiste.

---

### 35. I salti di fase mentre la parte suona, e come diventano «il clap in battere» 🔴 (2026-09-10, misurato su band vera — causa trovata, non corretta)

Segnalazione, sul live `Flamingo Marco 09.07.26`: *«se il batterista rallenta o
velocizza tipo di 2 bpm, l'app non riaggancia immediatamente, nonostante si
senta che la percussione è fuori da cassa o rullo. Poi a un certo punto
addirittura l'app accelera talmente tanto che sposta l'uno al quarto successivo,
quindi il clap suona addirittura in battere.»* E sul brano 1: *«esce
costantemente e resta spesso fuori, in certi punti aumenta senza senso
esageratamente.»*

**Il BPM pubblicato non spiega niente di tutto questo**, ed è per questo che
non si era mai visto: sul brano 1 sta fra 80.5 e 84 per tutto il corpo del pezzo,
con confidenza fino a 1.00 e copertura 1.00. La colonna che l'utente sente è
un'altra.

#### Lo strumento che mancava

`VPTrack --pulses file` scrive, blocco per blocco, la **fase dell'orologio**
(battito e battuta) accanto al tempo pubblicato. Da lì si ricavano due cose che
prima non si potevano vedere:

- la **velocità istantanea della griglia**, cioè quella su cui i colpi sono
  davvero programmati, come derivata della fase;
- i **salti**, cioè i blocchi in cui la fase si sposta molto più di quanto il
  tempo preveda.

E `s.clockBpm` è stato aggiunto allo snapshot. Nota: **è uguale a `s.bpm`** —
`tr.clock.tempoBpm` è il bersaglio, non la velocità istantanea — quindi la
deriva va ricavata dalla fase, non da lì. Costa un tentativo saperlo.

#### Cosa dicono i numeri

Velocità istantanea della griglia contro il tempo dichiarato:

| | dichiarato | griglia istantanea | strappi oltre il 4% |
|---|---|---|---|
| brano 1 (swing) | 82.3 | **65.7 - 92.2** | 6 volte, 7 s su 245 |
| brano 3 (Sally) | 104.4 | **23.4 - 123.8** | **32 volte, 27 s su 269** |

E i salti veri, letti sulla fase grezza — ognuno in **un solo blocco da 2.6 ms**,
con la parte che suona:

| a | salto fase battito | in ms | salto fase battuta |
|---|---|---|---|
| 27.42 s | −0.438 battiti | **−275 ms** | −0.109 |
| 49.08 s | −0.302 battiti | **−169 ms** | −0.074 |
| 216.54 s | −0.214 battiti | −124 ms | −0.052 |
| 264.60 s | +0.373 battiti | +205 ms | +0.094 |

Il brano 1 ne ha **zero**: il suo problema è la deriva continua, non i salti.

#### Come si diventa «il clap in battere»

La fase della battuta si sposta ogni volta di **esattamente un quarto** del
salto del battito (−0.438/4 = −0.109, −0.302/4 = −0.076, +0.373/4 = +0.093):
il conteggio viene trascinato con la griglia, com'è giusto per un singolo
salto — ognuno è sotto il mezzo battito, quindi preso da solo è una correzione
di fase legittima.

**Ma si sommano.** I tre salti all'indietro fanno **0.954 battiti**, meno 0.373
in avanti: **0.58 battiti netti** di spostamento del conteggio rispetto alla
musica, in un brano solo. Ripetuto, l'uno arriva sul quarto successivo. Nessun
salto singolo è illegittimo; la somma sì, e niente la sorveglia.

#### Dove sta nel codice

Due punti chiamano `snapPhase` con la parte in corso:

- `BeatTracker.cpp:1421` — dopo una ricostruzione della griglia. Ben educato:
  mette `sounding = false` e `waitForQuantize`, cioè **ferma la parte e la fa
  rientrare quantizzata**.
- `BeatTracker.cpp:1561` — sulla transizione in FOLLOWING:
  `follower.snapPhase (songPhase, ! sounding)`, con l'unica condizione
  `|errore| > 0.12` battiti. **Nessun limite superiore, e la parte non viene
  fermata.** È da qui che passano i 275 ms.

Il commento accanto dice: *«Sounding, a snap is a stroke... Above the size the
steering loop would take seconds over, it is worth the stroke»*. Il ragionamento
regge per 0.15 battiti. Per 0.44 non regge: quello non è un flam, è la parte che
si sposta di un quarto di battito sotto le mani di chi suona.

#### CORRETTO: la risposta al drop di griglia è graduata (10/09/2026)

**Prima due correzioni a quello che c'è scritto sopra**, entrambe trovate
strumentando invece di ragionare:

1. **Non è l'accumulo.** Avevo scritto che i salti nella stessa direzione si
   sommano fino a spostare l'uno. Falso: al sito che li produce
   `keepBarInStep` è già `true`, quindi **il conteggio segue la griglia**. Il
   rilevatore di «battuta ruotata» che avevo scritto scattava proprio quando il
   conteggio seguiva *correttamente*. Era sbagliato lo strumento.
2. **Non è il sito 1561.** Ci ho messo il tetto per primo e i salti sono rimasti
   **identici**: `VP_SNAP_LOG` su ogni chiamata dice che **tutti e sette** i
   salti grossi vengono dal sito **1421**, il percorso di ricostruzione della
   griglia.

**La catena vera.** `BeatDecoder::checkGridPhase` fa scorrere l'ancora della
griglia sulla fase del fold — è una correzione di *fase*, non un tempo nuovo —
e incrementa `gridSerial`. Il tracker legge `gridSerial` come «pulsazione
nuova» e reagisce nel modo previsto per quel caso: **ferma la parte, mette la
griglia esattamente sul battito accettato, rientra quantizzato**. E
`checkGridPhase` piega il suo scarto sulla griglia più vicina, quindi può
legittimamente valere **fino a mezzo battito**.

Strumentato su Sally, tutte e sette le uscite vengono da lì:

```
GRID DROP [reset] t= 27.4  bpm= 95.5  comb=102.2
GRID DROP [reset] t= 49.0  bpm=109.3  comb=104.3
GRID DROP [reset] t=216.5  bpm=102.7  comb=101.5
GRID DROP [reset] t=264.5  bpm=107.8  comb=103.8   ... e altre tre
```

**La correzione: la risposta è graduata come quella di un musicista.** Sopra un
terzo di battito la griglia è davvero altrove e fermarsi per rientrare è
giusto. Fra un ottavo e un terzo è una piegata: si sposta subito una parte, con
un tetto di 0.20 battiti, e il resto lo chiude l'anello di fase — **senza
fermare la parte**, perché un percussionista che è un quinto di battito fuori
non si ferma, si appoggia.

| Sally | prima | dopo |
|---|---|---|
| salti di fase | **7** | **3** |
| la parte si ferma e rientra | **7 volte** | **1 volta** |
| correzioni di mezza taglia | 0.31-0.39 battiti | **tagliate a 0.195** (116 ms) |

Il salto da 0.438 resta e prende la strada del fermarsi-e-rientrare, che per
quella taglia è la cosa giusta.

Regressioni: `VPAlign` **identico**, `--bar` 10/0, `--tempo-slow` 10/0,
`--level` 15/1, `probe_bar` con **un battito su 2831** che cambia casella
(0:1793→1792, 1:399→400) e tutto il resto invariato, ingresso sull'uno e
rotazioni comprese.

Il tetto messo anche al sito 1561 è rimasto: non è lui a produrre questi salti,
ma passava `! sounding` a `keepBarInStep`, e `snapPhase` documenta da sé che
così «the count is silently rotated by a quarter». Ora passa `true` in entrambi
i casi.

- [ ] **Il brano 1 non ha salti** e sta comunque fuori: 6 strappi oltre il 4%
  con la griglia che scende a 66 BPM contro 82 dichiarati, e nessun drop di
  griglia nel corpo del pezzo. Quello è il percorso di *steering*, ed è un
  secondo difetto, indipendente da questo.
- [ ] **Perché `checkGridPhase` scatta sette volte** in un brano resta da
  capire: è il fold che continua a dire che la griglia è sfasata. Se il fold ha
  ragione, il vero difetto è a monte; se ha torto, va guardata la sua soglia.
- [ ] **`gridSerial` fa due lavori.** Lo incrementano sia un cambio di
  pulsazione vero (snap d'ottava, watchdog) sia una correzione di fase. Il
  consumatore non può distinguerli e la risposta giusta è diversa. Separarli
  sarebbe più pulito di graduare a valle sulla taglia dello scarto.
- [ ] **Il brano 1 non ha salti** e sta comunque fuori: 6 strappi oltre il 4%
  con la griglia che scende a 66 BPM contro 82 dichiarati. Quello è il percorso
  di steering, non lo snap, ed è un secondo difetto da misurare a parte.

#### Il banco nuovo

`scratch/hist.py` piega l'energia degli attacchi sulla fase dell'orologio e
misura quanto la griglia sta sulla musica (struttura = picco/media
dell'istogramma a 24 bin) e dove sta il picco. È robusto alle terzine, che il
primo tentativo — la risultante della prima armonica — non era: su materiale
swingato l'energia cade a 0, 1/3 e 2/3 e la risultante si annulla **anche con
l'aggancio perfetto**. Il primo tentativo dava «FUORI» su tutto il brano 1 ed
era un artefatto della metrica. Va portato in `scripts/`.

---

### 36. `03 FEEL`: l'ottava doppia su un brano intero, e il bilancio dei tre brani veri 🔴 (2026-09-10)

Terzo file reale, 4:19. **Il tempo vero è ~104 BPM e l'app suona a 205-213 per
tutto il brano**: conta gli ottavi come battiti. Autocorrelazione della banda
alta (charleston e piatti) sul tratto 30-150 s:

| periodo | 52 | 86 | **104** | 138 | **208** |
|---|---|---|---|---|---|
| autocorrelazione | 0.238 | −0.004 | **0.371** | 0.005 | 0.289 |

104 vince su 208 con margine. La banda bassa è debole ovunque (0.005-0.036): la
cassa è sparsa o coperta, quindi il livello metrico deve venire dalla banda
alta, che è piena di ottavi — è **esattamente** il caso che la skill descrive
come «a dense grid is not a right grid»: il charleston riempie gli ottavi,
quindi la griglia doppia ha copertura 1.00, residuo 0.03 e sembra sana a ogni
test che il veto possiede.

Peggio: `barTrusted` è **SI** da 40 s. L'app piazza con fiducia una battuta su
una griglia doppia, quindi l'uno cade su quello che la band sente come «e».

- [ ] Verificare se `recentStrengthAlternation()` scatta qui. Esiste apposta per
  questo caso e la misura dice che non ha corretto. Se non scatta, capire
  perché; se scatta e viene vetata, capire da cosa.
- [ ] `probe_matrix` ha una riga `rock 8vi` che è lo stesso difetto sintetico
  (rapporto 1.44 a 81 BPM). Questo file è la sua versione reale e va aggiunto
  al banco.

#### Il bilancio dei tre brani veri

| | brano 1 (swing, 82) | Sally (104) | FEEL (104) |
|---|---|---|---|
| ottava | giusta | giusta | **doppia** |
| salti di fase | 0 | 7 → **1** | 3, tutti sotto i 15 s |
| la parte si ferma | 0 | 7 → **1** | 1, in acquisizione |
| strappi griglia > 4% | 6 | 32 | 4, tutti sotto i 27 s |
| deriva di fase | ±60-170 ms | ±70-170 ms | ±12-60 ms |

**Tre difetti diversi, non uno.** La correzione dell'item 35 ha chiuso quello di
Sally e non tocca gli altri due. Chi riprende non deve cercare una causa sola.

#### Cosa costa davvero «rientrare subito», in quattro pezzi indipendenti

Misurato attraverso tutti e tre i brani, il rientro ha quattro costi in serie, e
ognuno ha un padrone diverso:

1. **Accorgersene.** Il fit lungo è 8-24 battiti, e a tempo lento la sua
   finestra è nove secondi; il fold è l'altra via. Sul brano 1 è questo il costo
   vincolante (item 31: quattro secondi solo per uscire da FISSO).
2. **Decidere.** L'arbitrato rete/pettine: 35% di autorità × 22% per battito =
   7.7% effettivo, **tredici battiti**. Misurato non tarabile su un brano solo
   (item 34), serve il banco dei sei.
3. **Muoversi.** L'orologio: o un salto (istantaneo ma è uno strappo sotto le
   mani) o lo steering, limitato al 20%. **Graduato nell'item 35.**
4. **Il pavimento.** Quanto vicino può arrivare: era 13.8 ms a 52 BPM, ora 6 ms
   a ogni tempo (item 32).

Corretti oggi il 3 e il 4. Aperti l'1 e il 2.

**E il pavimento fisico va detto:** l'orologio piega la velocità invece di
saltare, apposta, perché nessun colpo venga mai suonato due volte o saltato. Al
rail del 20%, chiudere un quarto di battito costa **1.25 battiti** — circa un
secondo a 104 BPM. Quello è il minimo per una correzione che non si senta come
uno strappo. Più veloce di così è un salto, ed è esattamente quello che l'item
35 ha appena messo sotto tetto. **«Immediatamente» ha come pavimento un
battito**, e per avvicinarcisi si lavora sul punto 1, non sul punto 3.

---

### 37. La deriva leggera durante il brano: il default di `FollowStrength` era il peggiore dei tre 🟢 (2026-09-10, misurato su band vera e corretto)

Richiesta: *«risolvere i problemi di drift leggeri delle percussioni durante il
brano, rendere tutto il più preciso possibile»*.

**Il conto di partenza.** Un errore di tempo dell'1% è una deriva di fase di
10 ms al secondo. A 82 BPM, l'app che gira a 82.8 invece di 82.0 guadagna 170 ms
in diciassette secondi — che è l'ordine di grandezza misurato sui brani veri
(±60-170 ms). La deriva di fase *è* in gran parte il tempo leggermente sbagliato,
più l'anello che la insegue.

**Quanto stringe l'anello lo decide `FollowStrength`**, e la tabella in
`TempoFollower` è (tau, steerLim, steerCeil, dGain):

| | tau | steerLim | steerCeil | dGain |
|---|---|---|---|---|
| low | 1.60 | 0.018 | 0.10 | 0.3 |
| medium | 0.90 | 0.035 | 0.18 | 0.8 |
| high | 0.70 | 0.050 | 0.25 | 1.2 |

`docs/STATUS.md` e la skill registravano già che **HIGH fa escursioni due o tre
volte più grandi di LOW senza guadagno in rms** su `probe_steer` (6.7% contro
2.6% di peggiore a 144 BPM), e che il default era stato lasciato su HIGH perché
*«quel banco non ha dentro nessun cambio di tempo vero, che è l'unica cosa per
cui HIGH esiste»*.

**Adesso c'è materiale con cambi di tempo veri** — una band dal vivo che
respira — e **LOW vince lo stesso.** `VPTrack --follow` (nuovo) sui due brani:

| brano 1 (82 BPM) | struttura | spost. mediano | peggiore | strattoni dev.std |
|---|---|---|---|---|
| **low** | **3.88** | **30.3 ms** | **182 ms** | **1.77%** |
| medium | 3.77 | 60.7 ms | 152 ms | 1.81% |
| high *(default)* | 3.61 | 30.3 ms | **364 ms** | 2.11% |

| Sally (104 BPM) | | | | |
|---|---|---|---|---|
| **low** | **3.29** | **47.9 ms** | **240 ms** | **2.86%** |
| medium | 3.14 | 95.7 ms | 263 ms | 3.00% |
| high *(default)* | 3.04 | 71.7 ms | 263 ms | **3.57%** |

LOW vince su struttura (entrambi), spostamento mediano (entrambi) e dev.std
degli strattoni (entrambi).

**E `--level` lo conferma sul sintetico**, sulla colonna della fase a 91 BPM:

| livello | HIGH | LOW |
|---|---|---|
| 0 dB | 59.0 ms | **33.1 ms** |
| −6 dB | 25.6 ms | **14.4 ms** |
| −12 dB | 32.3 ms | **14.4 ms** |
| −18 dB | 33.2 ms | 42.8 ms (peggio) |

A 168 BPM è mezzo millisecondo peggio (6.6 → 7.1), su numeri già dentro il
bersaglio degli 8 ms.

**Corretto: il default passa a LOW.** Era `high` in due punti, entrambi cablati
— `Types.h:368` e `MainComponent.cpp:929` — e **non esiste nessun controllo
nell'interfaccia**: non era una manopola, era una scelta fissa.

Regressioni: `--level` **15/1** (stesso unico FAIL), `--bar` 10/0,
`--tempo-slow` 10/0, `VPAlign` **identico riga per riga** (imposta le tarature
esplicitamente per riga, quindi il default non lo tocca).

#### Il costo di LOW sui gradini: misurato, ed è zero

Il dubbio lasciato aperto qui sopra è chiuso. Due fixture nuove attraverso il
motore completo — kick/rullante/charleston, 100 BPM per 63 s poi il gradino,
`VPTrack --follow` — e si misurano due cose separate: quando il **tempo
dichiarato** entra nel ±2% e ci resta due secondi, e quando ci entra la
**velocità istantanea della griglia**, che è quella su cui i colpi cadono.

| gradino | taratura | tempo dichiarato | griglia vera | sovraelongazione |
|---|---|---|---|---|
| 100 → 124 (+24%) | high | 16.7 s | 19.1 s | +0.3% |
| 100 → 124 (+24%) | **low** | **16.7 s** | **19.1 s** | +0.2% |
| 100 → 107 (+7%) | high | 1.2 s | 2.2 s | +7.1% |
| 100 → 107 (+7%) | **low** | **1.2 s** | **2.2 s** | +7.7% |

**Identici al decimo di secondo.** LOW non costa niente sui gradini, e il motivo
è strutturale: `FollowStrength` governa la *fase*, mentre il costo di un gradino
sta nell'*accorgersene*, che è il decoder e non cambia. Il compromesso che si
temeva non esiste; LOW è un guadagno netto.

- [x] ~~Il costo di LOW su un gradino di tempo brusco non è misurato~~ — misurato,
  è zero.

#### Ma le due righe dicono un'altra cosa, e va perseguita

**Una spinta realistica del 7% — un batterista che accelera — costa 1.2 s per il
tempo e 2.2 s per la griglia, con una sovraelongazione del 7%.** La griglia va a
~114 BPM quando la band è andata a 107, poi rientra. Segnalato dall'utente in
due modi diversi: *«quando deve saltare tanto di bpm lo fa accelerando o
decelerando invece di passarci direttamente»* e *«su Baila e su tanti ci sono
punti in cui il batterista rallenta o accelera e l'app arranca un po' prima di
riallinearsi»*.

La sovraelongazione **non è un difetto dell'anello**: è la fase accumulata che
viene ripagata. Il tempo ci mette 1.2 s a essere notato, in quel tempo la fase
accumula, e la griglia deve correre più veloce del bersaglio per recuperarla.
La sua taglia è limitata da `steerCeil` (0.10 a LOW, 0.25 a HIGH).

Quindi la scala è: **meno ritardo nell'accorgersi = meno fase accumulata = meno
sovraelongazione.** Non si riduce stringendo l'anello — si riduce accorgendosene
prima, che è il punto 1 del bilancio dell'item 36. È la stessa conclusione da
tre direzioni diverse.

- [ ] Il gradino da +24% a **16.7 s** è un numero pessimo, ma un salto del 24%
  è un altro brano, non un batterista. Non confondere i due casi: la riga da
  perseguire è quella del 7%.
- [ ] **Non c'è un controllo per questo.** Tre tarature nel codice, nessun modo
  di sceglierle. Se il palco vuole HIGH per un pezzo e LOW per un altro, oggi
  non si può.
- [ ] **La deriva residua resta il punto 1** del bilancio dell'item 36:
  accorgersi prima. LOW stringe l'anello, non accelera l'accorgersi.

---

### 38. Punto 1, «accorgersene prima»: il banco reale, il numero, e perché non si tara 🔴 (2026-09-10)

Richiesta: *«lavora sul punto 1: accorgersene prima»*. Prima di toccare
qualunque cosa serviva **misurare il ritardo**, che fino a oggi era solo dedotto.

#### Il banco: `scripts/analysis/bench_live.py`

Cinque brani dal centro del live `Flamingo Marco 09.07.26`, 2:20 ciascuno, presi
dal **centro** del pezzo per evitare intro e code, che sono un altro problema.
Per ognuno una stima indipendente del tempo vero (`tempocurve.py`, tempogramma a
finestra di 12 s). Il batterista deriva del **2.6-4.2% dentro ogni brano**:
80.7-84.1, 105.0-108.3, 101.6-105.7, 84.4-86.8, 87.6-89.9 BPM.

Riporta tre difetti separati:

- **ritardo** — di quanti secondi l'app è indietro, dal massimo della
  correlazione incrociata. È il punto 1.
- **errore** — scarto medio del passo, **ripiegato sull'ottava**, perché
  altrimenti un errore del 100% seppellirebbe tutto il resto.
- **strattoni** — deviazione standard della velocità *istantanea* della griglia.

Due cose imparate costruendolo, entrambe costate una corsa:

1. **Serve un lead-in di silenzio.** Tagliando a metà brano non c'è nessun
   momento di quiete, quindi `alreadyPlaying` non si apre e **la parte non entra
   mai** — è il buco stretto dell'item 33, che ha morso il mio stesso banco.
   Tre secondi di silenzio davanti a ogni taglio, e le curve di verità spostate
   di altrettanto.
2. **Il ritardo non è misurabile quando la correlazione è bassa.** Con r=0.17 un
   brano ha riportato **−4.00 s**, cioè l'app *in anticipo* sul batterista, e
   trascinava la media. Sotto r=0.50 il ritardo si stampa ma non entra
   nell'aggregato.

#### La base, confermata due volte

| brano | ritardo | r | errore | strattoni | ottava |
|---|---|---|---|---|---|
| 1 | +7.75 s | 0.85 | 1.09% | 2.28% | — |
| 2 | +5.25 s | 0.85 | 0.72% | 1.74% | — |
| 3 | +3.50 s | 0.49 | 1.12% | 2.25% | — |
| 4 | +7.50 s | 0.81 | 0.78% | 1.17% | **100%** |
| 5 | +5.00 s | 0.31 | 0.99% | 1.11% | **100%** |
| **MEDIA** | **+6.83 s** | 0.66 | **0.94%** | **1.71%** | **40%** |

**Il numero del punto 1 è ~6.8 secondi.** L'app è quasi sette secondi indietro
rispetto alla deriva del batterista. Il *passo* invece è preciso: 0.94% una
volta tolta l'ottava.

**E due brani su cinque sono suonati all'ottava sbagliata.** Con *Baila*, *FEEL*
e questi, fanno **quattro su otto** dei brani veri misurati finora. L'item 36
non è un caso isolato: è il difetto più frequente sul materiale reale.

#### Il banco è ripetibile, e verificarlo era necessario

Tre corse dello stesso binario danno serie di BPM **byte-identiche**, e la corsa
completa ripetuta dà la stessa media al centesimo. `VPTrack` aspetta già
`analysisCompletedSamples()` a ogni hop.

**Ma una base precedente, 5.30 s, non è riproducibile** e non so da quale
binario venisse: stesso codice, stessi file di verità, banco deterministico, e
oggi lo stesso comando dà 5.80 (poi 6.83 con il filtro su r). **Quella base è
scartata e con essa lo sweep che ci era stato misurato contro.** Chi riprende:
il baseline si rimisura, non si eredita.

#### `kRateLive`: il compromesso è reale e non è un guadagno

L'aritmetica dice che una piegatura del primo ordine a α per battito lascia
(1−α)/α battiti di ritardo: a 0.22 sono **3.5 battiti**, che spiegano metà del
ritardo misurato. Alzarla:

| `kRateLive` | ritardo (r≥0.5) | strattoni |
|---|---|---|
| **0.22** (base) | **6.83 s** | **1.71%** |
| 0.32 | 5.67 s | 2.81% |

Guadagna 1.2 s di ritardo e **triplica gli strattoni sul brano 2** (1.74% →
5.38%). Non è un guadagno pulito, ed è la seconda costante di seguito che si
comporta così. **Non spedita.**

- [ ] **La strada non è tarare un guadagno.** Due sweep di fila
  (`kCombPull` item 34, `kRateLive` qui) sono risultati non monotoni o con un
  costo pari al guadagno. Il ritardo è strutturale: fit centrato + piegatura del
  primo ordine. Si toglie cambiando la *forma* della stima, non il suo guadagno
  — per esempio una stima non centrata (fit pesato verso i battiti recenti) o un
  predittore esplicito della deriva invece di un filtro che la insegue.
#### `kLiveLead` serve, e il difetto è che esiste in un ramo solo

Strumentato (`VP_LEAD_LOG`, poi rimosso), sul brano 1 che sta in `live` per il
72% del tempo: `haveLong` è vero al **99%**, l'anticipo applicato vale **0.369
BPM in media, lo 0.447% del tempo**, e **non viene mai clampato**. Funziona.

Sui brani 2 e 4 il log non stampa nulla: quel ramo non viene quasi mai
raggiunto. La distribuzione dei regimi sul banco lo spiega:

| brano | CERCO | FISSO | VIVO | ritardo |
|---|---|---|---|---|
| 1 | 4% | 23% | **72%** | +7.75 s |
| 2 | 0% | **63%** | 35% | +5.25 s |
| 3 | 3% | 27% | 69% | +3.50 s |
| 4 | **69%** | 0% | 30% | +7.50 s |
| 5 | 25% | 0% | 73% | +5.00 s |

**L'estrapolazione in avanti esiste solo nel ramo `live`.** Gli altri due usano
stime senza nessun anticipo, e sono le più in ritardo che ci siano:

- `unknown` prende il **fit lungo grezzo** — 24 battiti, centrato **12 battiti
  indietro**, che a 86 BPM sono **8.4 s** di puro ritardo di centratura. Il
  brano 4 ci passa il 69% del tempo e misura +7.50 s. I due numeri sono lo
  stesso numero.
- `fixed` prende `fixedAnchorBpm`, una media corrente del fit lungo: ritardo
  doppio.

#### Provato: dare l'anticipo anche a `unknown`. Misurato bene e misurato male.

| | base | con l'anticipo in `unknown` |
|---|---|---|
| **brano 4** | +7.50 s | **+3.00 s** |
| banco reale, media | 6.83 s | **5.50 s** |
| correlazione media | 0.66 | **0.70** |
| errore / strattoni | 0.94% / 1.71% | 0.97% / 1.77% |
| `probe_matrix` aggancio medio | 5.25 s | **4.88 s** |
| `probe_matrix` uscite | 98 | **97** |
| `probe_tempo_step` | — | **identico** |
| **`VPAlign` a 132 BPM, fase rms** | **6.1 ms** | **118.9 ms** |
| `VPAlign` scatti a 100 e 132 BPM | 0 e 0 | **27 e 33** |

Il fit lungo è il **preciso**; il corto porta con sé il proprio rumore
(0.5-1.15 BPM battito-per-battito a tempo fermo). Su materiale pulito quel
rumore diventa fase, e VPAlign lo dice in modo brutale.

**Provato: gattare l'anticipo su «il tempo si sta muovendo»** (`haveWindow &&
|trend| > kLiveTrend`, lo stesso test della macchina dei regimi). `VPAlign`
torna **identico alla base** — e il banco reale torna **identico alla base
anche lui**: l'anticipo non scatta mai. Ed è circolare per costruzione: quei
brani stanno in `unknown` **proprio perché** quel test è falso. Ripristinato.

È lo stesso muro documentato nel ramo `live`, raggiunto dall'altra parte: *«il
movimento del fit corto a tempo fermo è 0.5-1.15 BPM, e una band che accelera da
118 a 126 in dodici secondi ne muove 0.33 — il segnale sta sotto il rumore di un
fattore due, quindi nessuna soglia su di esso separa niente»*.

- [ ] **La forma della soluzione non è una soglia, è una contrazione.** Invece
  di decidere se estrapolare, **pesare** l'anticipo per il rapporto
  segnale/rumore: `lead × σ²signal/(σ²signal + σ²noise)`, con il rumore stimato
  da `shortFitRate` (che esiste già come diagnostico proprio perché fu lui a
  mostrare il muro). A tempo fermo il peso va a zero da solo e la precisione del
  fit lungo resta; su una deriva vera il peso sale. Non è stato provato.
- [ ] **Oppure il difetto vero è un altro: `unknown` non è un regime di
  regime.** Il brano 4 passa il **69%** della sua durata nel regime di
  *acquisizione*, e il brano 5 il 25%. Un pezzo che per due terzi non è né
  FISSO né VIVO è una classificazione fallita, e `unknown` è il ramo meno
  tarato dei tre. Capire perché `mayFix` e `moving` sono entrambi falsi per
  minuti interi su una band vera potrebbe valere più dell'anticipo.
- [ ] Il brano 6 non ha una curva di verità utilizzabile (2 punti buoni). Il
  banco è a cinque.

---

## Standby

Lavoro **non bloccante** se usi solo **PATTERN** (motore sintetico / `GrooveEngine`, switch LOOP spento). Il codice del ciclo Codex (tempo rapido, suddivisione congas, canceller, epoch/make-up, 156 BPM, test) è già nel tree; qui resta la **chiusura formale** e l'integrazione **loop registrati** (altro documento).

### 39. Cambio violento: il buco fra transizione e ottava è chiuso sulla mandata 🟢 (2026-09-10)

Il percorso rapido accettava al massimo il 25% e solo entro un quarto d'ottava;
quello d'ottava, correttamente, decideva soltanto relazioni metriche. Un salto
come 120→160 non apparteneva a nessuno: arrivava al watchdog dopo **23.6 s**.

Su linea/mixer il detector può ora accettare fino al 65%, ma solo lontano almeno
0.15 ottave da una relazione ×2/÷2 e dopo **tre** intervalli causali coerenti.
Restano invariati bordo brusco, scatter, range e percorso microfono. Dopo la
conferma il pettine vecchio è escluso finché il fit da otto battiti non è stato
ricostruito: senza questa quarantena 120→160 veniva riconosciuto e poi ributtato
a 53.3 BPM dal pettine ancora fermo sul tempo precedente.

| salto | prima | dopo |
|---|---:|---:|
| 120→150 | 12.8 s | **1.2 s** |
| 120→160 | 23.6 s | **1.1 s** |
| 100→160 | 10.1 s | **1.1 s** |
| 160→100 | 12.0 s | **1.8 s** |
| 120→90 | 10.7 s | **2.0 s** |
| 90→120 | 20.7 s | **2.2 s** |

`probe_tempo_step` ora fallisce oltre 2.5 s o oltre 1 BPM. `probe_matrix
--quick` è identico byte per byte a HEAD: 7.37 s, 18 uscite, 13.37% fuori.
`VPTests --tempo-step` 9/0; `VPTests --tempo-slow` 10/0; `VPTests --bar` 10/0.
Su 300 s × 10 semi:
120 BPM 0 uscite e 160 BPM 0.1% fuori.

I salti 120↔60 e 140↔75 restano fuori apposta: sono relazioni quasi/esattamente
d'ottava e l'audio non può dire se è cambiato il BPM o la suddivisione. Lì la
risposta deterministica resta TAP o ÷2/×2. Il ritardo delle derive continue è
separato: due predittori provati in questa sessione hanno peggiorato il banco
materiali o la fase e sono stati rimossi; non dichiararlo risolto da questo fix.

La serata completa `Flamingo Marco 09.07.26.m4a` (97 minuti, mandata mixer) è
stata campionata in cinque centri-brano riproducibili con
`scripts/analysis/extract_live.swift`. Solo due curve tempogramma erano abbastanza
affidabili come riferimento (ritardo medio 5.12 s), quindi non sono verità
annotata. Sul centro di Sally la catena finale non ha fatto restart aggiuntivi e
ha chiuso a 102.51 BPM, ma `prec.py` misura ancora 94 ms di spostamento mediano
fra finestre e 258.4 ms nel caso peggiore: conferma che la deriva graduale resta
un problema distinto.

### 19. Salto di tempo: il decoder resta incastrato sul vecchio BPM (2026-09-04, SUPERATO dall'item 39 per i salti non d'ottava su linea)

Stato attuale: l'item 39 chiude il blocco per la mandata diretta; questa sezione
resta come cronologia della causa e dei tentativi precedenti. Restano aperte le
relazioni d'ottava e la validazione su microfono/stanza.

Segnalato dall'utente: *«se salto da una velocità all'altra sembra che non trovi
a volte il tempo. Lo ritrova solo se muovo il volume del mic alzandolo o
abbassandolo. Come se a volte uscisse e avesse difficoltà a rientrare nel tempo
se non dopo numerose battute.»*

**Riprodotto e quantificato** con `scripts/probe_tempo_step.cpp` (decoder puro,
niente rete e niente real-time; la riga per compilarlo è in cima al file). Ogni
salto è misurato due volte: normale, e con `notifyInputRestart()` al cambio —
che è esattamente ciò che muovere il gain del mic finisce per provocare, via il
gradino di livello che fa scattare `analysisEpoch`.


| salto         | delta | normale                | con input-restart |
| ------------- | ----- | ---------------------- | ----------------- |
| 120 → 132     | +10%  | 4.1 s                  | 0.5 s             |
| 120 → 150     | +25%  | 17.2 s                 | 0.8 s             |
| **120 → 160** | +33%  | **MAI**                | 0.8 s             |
| 100 → 160     | +60%  | 27.8 s                 | 0.8 s             |
| 160 → 100     | −38%  | 15.6 s                 | 0.6 s             |
| 120 → 90      | −25%  | 10.7 s                 | 0.7 s             |
| 90 → 120      | +33%  | 34.7 s                 | 24.7 s            |
| **140 → 75**  | −46%  | **MAI** (deriva a 150) | 0.8 s             |
| 75 → 140      | +87%  | 8.1 s                  | 0.8 s             |


«MAI» non è un'iperbole né lentezza: su un orizzonte di **182 secondi** dopo il
cambio, 120→160 continua a riportare `bpm=120.00` con `confidence=1.00`. È
certo, ed è sbagliato. La colonna di destra è il workaround dell'utente,
misurato: quasi tutto scende a 0.5–1 s.

**Causa.** Tre cancelli, tutti sulla stessa costante `kOctaveThreshold = 0.25`
(log2, cioè **±19%**) in `Source/AI/BeatDecoder.cpp`:

1. `transitionCandidateAllowed` (~riga 1172) — la strada *rapida* rifiuta un
  candidato oltre ±19% dal `bpm` commesso: `metricalConflict`. Prima ancora,
   `kTransitionMaxRelativeDelta = 0.25` rifiuta oltre ±25%: `outsideRange`.
2. `pullTowardsComb` (~riga 2028) — la strada *lenta e continua* si tira
  indietro esattamente sopra la stessa soglia («a different level entirely —
   not this path's business»).
3. `combDisagrees` (~riga 1631) — questo dovrebbe essere il portello di
  sicurezza (scatta solo *sopra* la soglia), ma sui casi «MAI» non arriva mai a
   far riancorare.

Il commento di `transitionCandidateAllowed` dice che i salti fuori range sono
«affare della macchineria dell'ottava, che ha tutto il buffer». Ma quella si
muove solo agli estremi (`kOctaveTooFast = 168`, `kOctaveTooSlow = 49`) o su un
hint metrico. **Un salto di media portata come 120→160 non è né un'ottava né un
estremo: cade in un buco che non è di nessuno.**

`notifyInputRestart()` funziona perché azzera tutto — `established`,
`intervalAcquired`, la griglia, lo storico dei fit — e rientra in
`TempoRegime::unknown`. Il suo stesso commento lo dice: *«A tempo called fixed
on a room is the most expensive thing to keep: it is designed to be stubborn.»*
La testardaggine è voluta (difesa dal rumore di stanza), ma **non c'è una via di
mezzo proporzionata**: o difende 120 per sempre, o cancella tutto.

**Perché non è mai stato visto:** l'unico test di gradino è **120→132, +10%**
(`"microphone/line confirms rising tempo step within two beats"`), dentro la
finestra. Nessun test supera il ±19%.

- [x] Riprodotto, quantificato, causa isolata (2026-09-04). Probe in `scripts/probe_tempo_step.cpp`.

**Causa esatta trovata (2026-09-04, seconda indagine).** Strumentando il decoder
sul caso 120→160: il comb riporta **106,67**, non 160. `log2(120/106,67) = 0,170`,
**sotto** `kOctaveThreshold = 0,25` — quindi `combDisagrees` è falso e il portello
di sicurezza non scatta mai. 106,67 BPM è un periodo di 0,5625 s, cioè **1,5 volte**
quello vero: il comb sta descrivendo una griglia che rifiuta la maggior parte dei
battiti di cui dovrebbe essere fatta.

È **la stessa trappola già documentata per la fase** nel commento di
`checkGridPhase` (`BeatDecoder.cpp`, ~riga 982): *«una griglia ancorata in levare
non è solo sbagliata, è stabile: ogni battito vero le cade fuori e viene rifiutato
come suddivisione, quelli che restano la fittano puliti… e non c'era niente nella
catena che potesse accorgersene.»* Per la fase l'hanno risolta col fold, che sta
fuori dal gate. **Per il tempo la stessa trappola è rimasta aperta**, e in quello
stato `residual`, `coverage` e `salience` sono tutti sani: il comb è l'unico che
sa, e sta all'11%, sotto ogni soglia.

**Tentativo di fix fatto e RIPORTATO INDIETRO (stessa giornata).** Watchdog
"stale grid": soglia più bassa del salto d'ottava ma voto molto più lungo
(`kStaleGridThreshold` in log2, `kStaleGridVoteBeats` battiti di disaccordo che
regge, con isteresi), e all'attivazione **non** adotta `combBpm` (che è sbagliato
anche lui) ma fa la riacquisizione mirata: molla griglia, fit ed evidenza del
fold, come `notifyInputRestart`.

- **Funziona sul bersaglio:** 120→160 passa da **MAI** a **40,5 s** (con voto 24
battiti; a 48 battiti erano 79 s).
- **Ma regredisce il banco a tempo costante:** a 170 BPM le corse con almeno
un'escursione passano da **4/10 a 10/10**. Verificato che *non* è la taratura
della soglia: con la soglia a 0,24 (appena sotto quella dell'ottava) la
regressione resta identica. È la riacquisizione stessa che costa un transitorio.
- **Perché il primo giro è stato scartato:** il metro usato ("corse con almeno
un'escursione >4%") non distingueva «peggiorato» da «si corregge, e correggersi
costa un transitorio».

**Secondo giro, con il metro giusto — RIUSCITO.** `probe_steady_tempo.cpp` ora
misura **quota di tempo passata fuori** ed **errore medio integrato** su tutti i
frame, non gli eventi. Su quel metro la baseline a 170 BPM è 3,1% di tempo fuori
e 1,93% di errore medio, e il primo watchdog la portava a **6,7% e 3,68%**: era
un peggioramento vero, non il costo del correggersi.

Causa di quel peggioramento, trovata: il watchdog scattava su **qualsiasi**
disaccordo sopra la sua soglia, **relazioni d'ottava incluse**. A 170 alcuni semi
acquisiscono a 85 (rapporto 2:1, `log2 = 1,0`): il watchdog rubava quei casi al
salto d'ottava e li gestiva peggio, riacquisendo a ripetizione.

**Correzione:** confinarlo alla banda che è davvero sua, cioè
`kStaleGridThreshold < apart < kOctaveThreshold`. Sopra quella soglia il
disaccordo è un livello metrico e lo possiede il salto d'ottava, che ha tenure,
salience e vote-hold fatti apposta per quell'argomento.

**Esito misurato** (`kStaleGridThreshold = 0,120`, `kStaleGridVoteBeats = 12`):


| banco                                | baseline                  | con watchdog               |
| ------------------------------------ | ------------------------- | -------------------------- |
| salto 120→160                        | **MAI** (>182 s a 120,00) | **22,6 s**                 |
| resto della tabella salti            | —                         | **invariata**              |
| tempo costante, ogni BPM da 60 a 170 | —                         | **identica alla baseline** |


Cioè: chiude il blocco permanente e **non costa niente** su nessun tempo del
banco a regime. Gli altri «MAI» rimasti (120→60, 140→75) sono relazioni d'ottava,
lasciate al loro path apposta: sono la classe indecidibile dell'item 1.

- [x] **Gate `VPTests` intera (2026-09-04): 610 passed, 7 failed — identico alla
  baseline**, stessi sette test e stesse righe (2 leak + i 5 RED a 50 BPM
  dell'item 1). Nessuna regressione.
- [ ] **Validazione ancora dovuta:** il banco a tempo costante è sintetico. Prima
  di considerarlo chiuso serve la stessa ampiezza di brani/stili con cui è tarato
  il resto del file, e l'**ascolto** — soprattutto: quando il watchdog scatta, la
  riacquisizione si sente come una ripresa o come un buco? Va provata dal vivo
  facendo il salto di tempo che l'ha fatto emergere.
- [ ] Il caso resta lento (22,6 s su 120→160). È una cassaforte, non un
  inseguitore: accorciare ancora il voto è tarare sul banco sintetico. Se serve
  più svelto, la strada è un segnale migliore, non una soglia più bassa.

- [ ] **Decidere il rimedio.** Non toccare `kOctaveThreshold` alla cieca: regge anche la difesa dal rumore di stanza e dalle letture a ottava sbagliata, ed è tarato su misure. La direzione che sembra giusta è una **terza via proporzionata**: evidenza coerente e ripetuta su un tempo fuori soglia per N battute → riacquisizione mirata (quello che oggi fa solo `notifyInputRestart`), senza buttare la fase né la battuta.
- [ ] Test di gradino oltre il ±19% (almeno 120→160 e 140→75), che oggi mancano del tutto.
- [ ] Verificare sul percorso vero (mixer/file con rete), non solo sul decoder: `VPProbe` non ha un `--tempo-step`, va aggiunto.
- [ ] Ascolto.

---

### A. Chiusura ciclo Codex (PATTERN, no loop registrati)

Non serve per suonare oggi in PATTERN; serve prima di commit/review "ciclo chiuso".

- [ ] **Ascolto** render sintetici (VPRender, non loop WAV): `conga-subdivision-dance.wav` e `conga-subdivision-marcha.wav` (es. in `/tmp/`). Densità 1/8 su congas/shaker; niente conga sullo step 0.
- [x] **Patch combinata feature-only**: `.superpowers/sdd/virtualperc-feature-combined.patch`, base `1cb93fc`, 33 file, SHA-256 `d3176210924f0f6246f42d945e595fced33b1a6b95bfa0888ee9e620b6ac6781`. Esclude `Assets/Loops/`, `Source/Loops/`, `HANDOFF_LOOP_DEBUG`, `.agents`, CMake e i lavori adiacenti clap/cembalo; apply/roundtrip verificato byte per byte; CRLF preservati.
- [x] **Report** in `.superpowers/sdd/`: numeri finali e descrizione veto/freeze allineata al codice in `makeup-phase-root-cause.md`, `makeup-phase-fix-report.md` e `phase-156-root-cause.md`.
- [x] **Lint / whitespace** sulla patch combinata: `git apply --check --whitespace=error-all` verde. Il repository non definisce un target lint/tidy/format dedicato; restano sette warning preesistenti in `Source/AI/OnnxSession.cpp`.
- [x] Snapshot pre-commit: `VPTests` due volte (`575/576`, poi `576/576`), `VPAlign` exit 0 con otto righe PASS, `VPTiming` exit 0, `VPRoom` exit 0 e build simulatore riuscita. Nessun ulteriore run va avviato senza approvazione esplicita.

Chiusura tecnica completata il **2026-09-04** senza commit. La chiusura musicale resta sospesa esclusivamente all'ascolto umano dei due WAV sopra; non dichiararla completata prima della conferma.

Artefatti singoli già prodotti (se servono): `rapid-tempo-complete-diff.patch`, `conga-complete-diff.patch`, `sparse-leak-fix-diff.patch`, `makeup-phase-fix-diff.patch`.

### B. Loop registrati (WAV)

Vedi `**docs/HANDOFF_LOOP_DEBUG.md**`. Switch LOOP/PATTERN, banco `Assets/Loops/dance`, debug iPad (gracchiio, 48 kHz, swing oltre 18%, ecc.). Fuori scope finché resti su PATTERN.

---

## Chiuso

*(sposta qui gli item con data breve quando sono integrati)*

---

## Note per chi riprende

- Skill tempo: `.claude/skills/realtime-tempo/SKILL.md`  
- Skill parti: `.claude/skills/percussion-patterns/SKILL.md`  
- Tempo lento ~50 BPM + hat ottavi: item 1 — root cause confermata (ottava bloccata sugli ottavi). Due versioni di `BeatDecoder::observeDownbeatCadence` provate e insufficienti (soglia forte; poi probabilità continua con istogramma a tempo-indicizzato): la curva di downbeat della rete non distingue le due ipotesi su questo materiale per via della stessa ambiguità 1-vs-3 dell'item 2. Non riprendere con un altro tentativo isolato su pBeat/pDownbeat — serve una feature spettrale nuova o una validazione multi-brano estesa (vedi item 1 per i dettagli).  
- Battuta: `BeatTracker::alignBarFromVotes` / `notifyBarReentry`, `maybeDetectBarReentry`, `barLocked` (item 2 chiuso). `VPTests --bar`.
- Cambio brano a START on: `loadInternalTrack` senza reset tracker (item 3)  
- Drift guard: settings + mute in render; non mescolare con `BandDynamics::wantsSilence` (item 4)  
- Suddivisione: STRUMENTI `subAuto` / `sub4` / `sub8` / `sub16`; AUTO deve adattare 1/4↔1/8↔1/16 sull'ottava (item 6)  
- Swing: oggi knob `swingSlider`; deve diventare tasto ON/OFF in STRUMENTI (item 7); warp in `GrooveEngine::humanDelay` (`kFullSwingBeats`)  
- STOP: oggi `VirtualPercussionEngine::stop` + `percussion.silence()` immediato; deve diventare trillo shaker + fade (item 8)  
- PARTE: select custom `StyleSelect` + DINAMICA (`MainComponent`); AUTO prima voce, default motore MARCHA / `grooveAuto` off (item 12)
- UI tasto battuta: tap su «L'1 è QUI» sblocca senza nudge (`barControlNudgeOnTap`, item 13)
- NATURALE: `GrooveEngine::setShakerNatural`, tasto in STRUMENTI, default off (item 11)
- Clap gate (item 10): `tr.barTrusted` dal tracker (lucchetto o istogramma sullo zero, muto in finestra di rientro). Ascolto render + brano OK; taglio/seek coperti da `VPTests --bar`.
- Brano: `trackLoadButton` / `trackPlayButton` / `trackTransport`; waveform+seek in SETUP (`TrackWaveform`, item 14)  
- ÷2 / ×2: **rimossi** (item 15, 2026-09-03); ottava sempre auto in `BeatTracker`  
- Ottava automatica: si muove **solo se non sta suonando nulla** (item 17, `BeatTracker::updateAutoOctave`). Il livello si sceglie in acquisizione e si tiene; ÷2/×2 restano la via manuale anche a parte suonante. Regressione: `VPProbe --trace --live --mixer plain 168` deve finire ~167, non 84.
- Assestamento a volte lentissimo (fino a 30 s) sullo stesso materiale che di solito prende 2 s: item 18, aperto, **non** inseguirlo prima di sapere se esiste fuori dal banco (`VPProbe --sync`).
- Chiusura Codex PATTERN: parte tecnica completata; resta l'ascolto umano in **Standby A**. Loop WAV registrati: **Standby B** + `HANDOFF_LOOP_DEBUG.md`
- Guadagno automatico analisi (item 16): `kMakeupClipGuardPeak` in `VirtualPercussionEngine.cpp`, attenua solo sopra 0.90 di picco. Test veloce dedicato: `VPTests --octave` (non lanciare la suite intera per iterare qui). Full-suite gate e ascolto ancora da fare.
# Priorità recupero diretto — 09/09/2026

Checkpoint credito limitato: rifinitura iniziale a due intervalli concordanti
mantenuta su linea; INFINITO centrale +2 s 95.43 -> 92.27 BPM, deriva media
17.5 -> 9.0 ms. Banco ridotto: stabilità sostanzialmente invariata, aggancio medio
7.28 -> 7.30 s (non miglioramento universale). Verifica nell'app/mixer ancora aperta.

Input brano/mixer prima del microfono esterno. Correzione dopo conferma accelerata
e verificata con 84 casi mirati; riconoscimento tardivo dei cambi BPM e residui
lenti ancora aperti. Misure, comandi e prossima azione in `HANDOFF_TEMPO.md`,
sezione «Follow-up recupero — 09/09/2026». Non dichiarare risolti tutti i deragliamenti.
