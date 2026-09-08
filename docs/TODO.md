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

**Non è il clock.** Prima ipotesi, misurata e scartata: `scripts/probe_steer.cpp`
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

## Standby

Lavoro **non bloccante** se usi solo **PATTERN** (motore sintetico / `GrooveEngine`, switch LOOP spento). Il codice del ciclo Codex (tempo rapido, suddivisione congas, canceller, epoch/make-up, 156 BPM, test) è già nel tree; qui resta la **chiusura formale** e l'integrazione **loop registrati** (altro documento).

### 19. Salto di tempo: il decoder resta incastrato sul vecchio BPM (2026-09-04, APERTO — riprodotto)

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
