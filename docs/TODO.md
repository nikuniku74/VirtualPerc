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
- [x] **Feature spettrale provata (Claude, 2026-09-04) — e qui l'item cambia natura: il caso è indecidibile dall'audio.** Piano, misure e numeri in **`docs/HANDOFF_OCTAVE_50BPM.md`** (leggerlo prima di riprovare qualsiasi cosa qui).
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



### 2. Capire qual è il primo quarto (e riallinearlo dopo un taglio)

L'app deve mettere l'**1** sul primo quarto del 4/4 su mixer e brano caricato, senza «L'1 è QUI». Se il musicista **taglia due quarti** e riprende in 4/4, deve riallineare l'1 in poche battute. Niente look-ahead; il clock del tempo non riparte; ruotare l'1 = solo `rotateBarIndex`.

Oggi: istogramma downbeat + armonia (`BeatTracker::alignBarFromVotes`). In play servono ~8 battute prima di ruotare. Un buco di due quarti apre una finestra coming-in di 4 battute (`notifyBarReentry`, 8 beat di evidenza, una rotazione); il decoder **non** riparte. `barLocked` **vieta** ogni rotazione automatica. Sul line/mixer l'1 tenuto resta da misurare con `VPBar` (prima: ~20–21/25); 1 vs 3 è quello che la finestra deve chiudere.

- [ ] **Baseline:** `VPBar` 4/4 line/file, lucchetto spento. Annotare quarto all'ingresso, tenuto 20 s, e stesso brano con buco di 2 quarti. I numeri di prima (lento o mai sul taglio) restano il confronto; il RED sotto è scriptato, non sostituisce questo banco.
- [x] **Test RED taglio:** `VPTests --bar` — following, tace 2 quarti, downbeat del modello su quello che il clock chiama 3. Entro 4 battute `beatInBar` è 0; decoder `analysisRestarts` invariato. Fill senza buco di livello non ruota. Con lucchetto non ruota. Seek: stessa finestra, niente epoch.
- [x] **Finestra di rientro:** `maybeDetectBarReentry` (picco del blocco vs `levelLoud`, non l'epoch da 4 s) + `notifyTrackSeek` → `BeatTracker::notifyBarReentry`. Senza `notifyInputRestart`. Una rotazione. RT: niente alloc/lock/I/O.
- [ ] **Ingresso file/mixer:** `VPBar` su `internalPlayer` / `kitMic` con rete vera. Se il file è peggio del line feed, è un bug di path, non si allentano i margini.
- [x] **Docs:** `.claude/skills/realtime-tempo/SKILL.md` + `docs/AUDIO_ENGINE.md` (rientro; lucchetto; clap `barTrusted`).
- [ ] **Gate:** `VPTests` intera + `VPAlign` invariato — non lanciare senza chiedere. Ascolto su brano caricato (taglio/seek di due quarti) e, se possibile, mixer. Il clap (item 10) ora legge `tr.barTrusted`.

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



### 5. Cambio parte (style) → esce dal tempo

Quando si cambia lo stile delle percussioni, a volte **perde il tempo**.

- [ ] Riprodurre: quale style, a che BPM, START già on, su che fonte (file/mixer).
- [ ] Tracciare: cambio al quarto (`PercussionEngine`), `alignPhrase`, voices, retrigger, clock. Il tempo non deve muoversi; sospetti: phrase/bar, attacco, buco di eventi.
- [ ] Fix minimo + test (cambio style a tempo locked, fase continua, niente pulse skip).
- [ ] Ascolto del cambio in play.

---



### 6. Pulsante AUTO (STRUMENTI) — densità che segue l'ottava del tempo

In **STRUMENTI**, **AUTO** oggi è solo un alias di **1/8**: non ascolta il brano e non cambia mai griglia. Deve diventare la modalità che fa quello chiesto sul raddoppio/dimezzamento.

**Manuale (fisso):** `1/4`, `1/8`, `1/16` restano quello che premi. Non si auto-adattano. Se il BPM raddoppia con 1/16 acceso, la parte resta a sedicesimi (e suona più veloce): è una scelta esplicita.

**AUTO:** tiene la **densità percepita** quando il tempo **raddoppia o dimezza** (ottava metrica). Copre tutte e tre le griglie, spostandosi tra loro. Il clock resta a sedicesimi; si cambia solo il thinning in `GrooveEngine`. Niente restart del clock, niente look-ahead.

Tabella (ottava 0 = lock "giusto", densità di riposo = **ottavi**, come oggi):

| Ottava del tempo | Griglia che suona |
|---|---|
| −1 (BPM circa **metà**) | **1/16** |
| 0 | **1/8** |
| +1 (BPM circa **doppio**) | **1/4** |

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



### 7. Swing: pulsante ON/OFF in STRUMENTI (niente knob)

Oggi lo swing è una **knob** 0–100% (`swingSlider` → `settings().swing` 0..1). Non serve una quantità: o è **dritto** o è **swing**. Togliere la knob; in **STRUMENTI** (accanto a shaker / congas / AUTO / 1/4 / 1/8 / 1/16) un pulsante **SWING** acceso/spento.

**ON** = swing pieno (terzina): l'"&" sta a **due terzi** del quarto (`kFullSwingBeats = 1/6`, `humanDelay` in `GrooveEngine`). **OFF** = 0, griglia dritta. Nessun valore intermedio in UI. Cambio come già per parte/swing: **al prossimo quarto**, non a metà beat.

- [ ] UI: pulsante in STRUMENTI; knob `swingSlider` / label / prefs come fader **via**. Prefs: bool (o 0/1). Default **off**.
- [ ] DSP: ON scrive `swing = 1`, OFF `swing = 0`. Non cambiare la formula del warp; se è sbagliata, sistemarla qui.
- [ ] **Verificare che lo swing sia fatto per davvero**, non solo "acceso":
  - quarti (step 0/4/8/12) **non** ritardati dallo swing;
  - l'"&" (step 2/6/10/14) a swing pieno atterra a **2/3 del quarto** (un sesto di beat tardi), non "un po' dopo";
  - "e" e "a" (step dispari) **seguono lo stesso stretch**, così a 1/16 la parte shuffle e non combatte gli ottavi ritardati;
  - `delayBeats` sempre ≥ 0 (mai anticipo);
  - humanize resta un jitter a parte, non si confonde con lo swing;
  - commit al prossimo quarto (test già in `TestMain`).
- [ ] Misura: test di timing (onset vs griglia / `VPTiming` o equivalente) + render `VPRender --swing 1 --click` e ascolto. Se i numeri non coincidono con 2/3, è un bug, non "feel".
- [ ] Loop registrati: restano standby; se il path loop rifiuta swing alto (`swing massimo 18%`), con il tasto ON restare sul path sintetico o documentarlo — non silenziare in silenzio.
- [ ] Docs skill percussioni: controllo = tasto, non knob.

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



### 10. CLAP e CEMBALO oltre shaker e congas 🟡 (2026-09-03, codice + ascolto ok — resta item 2)

Due voci in più, ognuna con **enable** e **volume proprio** (vedi item 9). Stesso clock a 16th.

**CEMBALO** = **tamburello** (2026-09-03: chiarito dall'utente — non un piatto). Suona con le prese VCSL *Tambourine*: colpo sul pulse, shake sul ritorno. Non è un pattern a parte: è **lo stesso mestiere dello shaker**, con un altro suono. Stessa griglia, stesso thinning (AUTO / 1/4 / 1/8 / 1/16, item 6), stesso swing, stessi accenti/pesi per stile, stesso humanize/dinamica. Acceso, suona sugli stessi step dello shaker; spento, tace. Shaker e cembalo possono stare entrambi on (due timbri sulla stessa parte) o uno solo. L'invariante "niente conga sul uno" **non** riguarda il cembalo: come lo shaker, può (e deve) cadere sul pulse.

**CLAP.** Non segue lo shaker. Suona **solo sul rullo**: in 4/4 i quarti **2 e 4** (backbeat), non ogni step.

**Allineamento (obbligatorio, item 2):** quei quarti devono essere l'1/2/3/4 **di quello che l'app sta sentendo**, non del conto interno se è ruotato. Se `beatInBar` è sfasato di due quarti, il clap "sull'1 e sul 3" dell'app in sala sta sul **2 e sul 4** (e il clap "sul rullo" sta sul 1 e sul 3). Non accettabile. Il clap **non parte** finché la battuta non è fidata (stesso criterio dell'item 2: 1 tenuto, non 1 vs 3 ambiguo, riallineato dopo un taglio di due quarti). Non "sistemare" il pattern a orecchio (mettere 2 e 4 per compensare un 1 sbagliato). Con lucchetto acceso vale il conto dell'utente.

Fatto: due nuovi `Stroke` (`clap`, `cembaloDown`/`cembaloUp`), sintesi fallback dedicata (`PercussionEngine::synthesizeCymbal` per il cembalo, un caso in `specFor` per il clap — nessun sample in `Assets/Percussion/`, non richiesto dall'item). `GrooveEngine::eventsAt` genera il cembalo riusando `spec.shaker[step]`/thinning/accento identici allo shaker (blocco separato, switch proprio); il clap è un pattern fisso (step 4/12) fuori da ogni tabella di stile, gated da `setBarTrusted`. `EngineSettings::clapEnabled/cembaloEnabled/clapVolume/cembaloVolume`, UI in STRUMENTI (tasti CEMBALO/CLAP) e FEEL (manopole), entrambi **off di default**. `kMaxEvents` 4→6 per il posto in più sulla stessa sedicesima.

**Buco noto (item 2, ascolto taglio):** il gate del clap ora legge `BeatTracker::barTrusted` (lucchetto, oppure istogramma sul zero fuori dalla finestra di rientro). Il proxy a due battute da `barRotations` è stato sostituito. Resta l'ascolto del taglio di due quarti (item 2 gate).

**Buco noto (loop registrati, standby):** clap e cembalo esistono solo nel motore sintetico — nessuno stem registrato. Con `VP_ENABLE_RECORDED_LOOPS` on (default) e una registrazione che prende il sopravvento, le due voci si spengono insieme al resto dei "single strokes" (vedi `HybridPercussionRenderer::Input`, commento vicino a `shakerVolume`). Fuori scope qui, è lavoro Standby B.

- [x] Articolazioni in banca (o sintesi fallback) + `Stroke` per clap e per cembalo — **sample registrati VCSL** (CC0): clap dalle prese d'insieme *Claps*, cembalo (= tamburello) da *Tambourine 1/2*. 13/13 articolazioni suonano da registrazione, nessuna sintesi (la sintesi resta solo come fallback senza asset). Scelte per attacco misurato, non a occhio: vedi `Assets/Percussion/ATTRIBUTION.md`.
- [x] Enable + volume per ciascuno.
- [x] Cembalo: riusa la logica/eventi shaker (stessi step, stessa suddivisione); solo sample/voce diversi.
- [x] Clap: solo step del rullo (default 2 e 4); silenzio altrove.
- [x] Clap gated sulla battuta fidata — `tr.barTrusted` dal tracker (item 2).
- [x] Test: `Tests/TestAiBeat.cpp` — cembalo segue 1/4 vs 1/16 come lo shaker (bit-identico allo shaker sui tre subdivision); clap solo su step 4/12 in ogni stile, muto se non fidato; volumi indipendenti (blocco "clap-cembalo" dopo "voice-volume", stesso schema dell'item 9). "non del bar index storto": non testabile fino a item 2 (non c'è ancora un caso reale di rotazione da riprodurre).
- [x] Ascolto render sintetico (2026-09-04): `VPRender --style rock --bpm 120 --bars 8 --click --no-shaker --no-congas --clap --cembalo` → `/tmp/clap-cembalo.wav`. Orecchio: clap sul 2 e 4 del click, cembalo da tamburello sulla griglia shaker. Il clock qui è forzato, quindi **non** copre l'allineamento al brano.
- [x] Ascolto in-app su brano caricato (2026-09-04): clap coincidente col rullo vero, cembalo al posto giusto. Mixer non provato in questa sessione.
- [ ] Dopo un taglio di due quarti: o tace o torna sul rullo vero. **Non ascoltato** — dipende da item 2 (rientro / 1 vs 3). Non chiudere il punto 10 prima di quello.
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
      due run separate con l'altra voce spenta, e solo i conteggi — la regressione
      che teme (due rollate → desync) si vede solo con entrambe accese.
      **Corretto:** aggiunto `"shaker and cembalo share one NATURALE roll when
      both are on"`, che gira con shaker *e* cembalo accesi e confronta il conteggio
      per singolo step, contando gli ornamenti perché non possa passare a vuoto
      (misurato: 69 ornamenti, 0 step disallineati).
- [x] «NATURALE never puts a conga on the first quarter's down-stroke» era vacuo
      due volte: shaker e cembalo erano off (quindi `shakerOn || cembaloOn` false e
      `soundingShaker` mai chiamato) e il loop chiedeva solo lo step 0, che NATURALE
      non tocca mai. **Corretto:** shaker acceso, tutti e 16 gli step percorsi, e
      `naturalOrnaments > 0` nell'assert perché non possa tornare vacuo
      (misurato: 51 ornamenti).
- [ ] Nota misurata, non un bug: con NATURALE on lo stream RNG condiviso si
      sposta, quindi le congas suonano **le stesse note** ma con velocity/delay
      humanize diversi (406 eventi su 408 a humanize 0.35). Nessuna nota cambia
      perché ghost e step dispari sono comunque scartati su 1/8 e 1/4. È
      l'invariante che il discard-buffer esiste apposta per proteggere, e non
      c'è un test che la fissi in questa direzione.

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
      scritto a mano in tre punti: l'ordine coincideva, ma se l'enum cambiava le
      etichette mentivano in silenzio. **Corretto (2026-09-04):** la voce 0 è
      AUTO, le altre leggono `vp::toString (GrooveStyle)`, e il conteggio è
      `StyleMenuOverlay::kCount = 1 + (int) GrooveStyle::count` — usato per la
      dimensione di `items[]`, il ciclo del costruttore e il layout in
      `resized()`. Uno stile nuovo nell'enum ora compare da solo nel menu con il
      nome giusto, invece di un `"?"` silenzioso.
- [ ] **Ascolto/touch:** STRUMENTI è passata a **nove** celle quadrate su una riga
      (`side = (W - 8*gap) / 9`): «NATURALE» in quello spazio accanto a «1/16».
      Da guardare su iPad vero prima di dire che è a posto. Unico residuo.

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
- [ ] Tracker/perc: dopo il seek, l'1 e la parte si riallineano al nuovo punto. Seek chiama `engine.notifyTrackSeek()` → finestra `notifyBarReentry` (item 2), **senza** bump di `analysisEpoch`. Test scriptato in `VPTests --bar`; resta ascolto su file.
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
>     che erano spariti dalla struct;
>   - il motore che li inoltra di nuovo al tracker
>     (`VirtualPercussionEngine::processBlock` → `setTempoOctave` /
>     `setTempoOctaveAuto`, e lo snapshot che riporta il valore vero invece di
>     `true` fisso);
>   - la UI: `halveButton` / `doubleButton` ai lati del numero di BPM, handler,
>     `applyTempoOctave` / `applyTempoOctaveAuto` / `refreshOctaveButtons`,
>     layout a tre colonne e prefs (`tempoOctave`, `tempoOctaveAuto`).
>
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

| caso | prima | dopo |
|---|---|---|
| **168** | 83.68, oscillazione **84.98**, mai stabile | **166.90**, oscillazione **2.02**, stabile in 3.6 s |
| 158 | 157.57 | 157.01 — invariato |
| 176 | 88.16 | 88.12 — dimezza ancora, ma **in acquisizione** (2 s) e poi fermo: corretto, 176 sta davvero sopra il limite |
| 132 | 132.36 | 132.39 — invariato |
| 104 | 103.53 | 103.67 — invariato |
| 200 (fisso) | 100.25, 3 salti, 1 battuta rotta | 100.02, **0 salti**, oscillazione da 2.10 a 0.13 |

- [x] Modifica in `BeatTracker::updateAutoOctave` (`Source/Tracking/BeatTracker.cpp`), con il commento che riporta la traccia sopra.
- [x] Verificato con `VPProbe --trace --live --mixer plain <bpm>` sulla matrice qui sopra. **Comando di regressione:** `VPProbe --trace --live --mixer plain 168` deve finire vicino a 167, non a 84.
- [ ] **Ascolto:** suonare un pezzo a ~168 che deriva e verificare che la parte non cambi densità a metà. Non fatto.
- [x] Gate `VPTests` intera **fatto (2026-09-04)**: 610 passed, 7 failed, tutte preesistenti (2 leak + i 5 RED a 50 BPM dell'item 1). Il congelamento dell'ottava sotto `sounding` non rompe nulla nella suite.

---



### 18. Assestamento lento e imprevedibile 🟡 (2026-09-04, misurato e circoscritto — resta il confine a 170)

Trovato durante l'indagine dell'item 17, **non risolto e deliberatamente non
toccato**. Con lo stesso identico materiale, la stessa corsa dà esiti diversi:

| tempo (fisso) | corse | tempo per assestarsi |
|---|---|---|
| 104 | 4 | 2.3 s, 1.7 s, **31.7 s**, e una che non ci arriva mai (oscillazione 9.7, 5 salti) |
| 108 | 1 | **29.0 s** |
| 100 | 4 | 1.9, 2.0, 2.1 s … e una da **27.0 s** |

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

  | BPM | corse con uscite (su 10) | errore peggiore | fuori più a lungo |
  |---|---|---|---|
  | 60 | **10** | **109%** | **288 s** |
  | 75 / 90 / 100 / 110 | 0 | — | — |
  | 120 / 132 / 140 / 150 / 160 | 1–3 | 4–5,5% | 2–3 s |
  | 170 | **4** | **50%** | **26,5 s** |

  Letto per bene, non è "il tracker è instabile":
  - **75–160 è sano.** I blip da 2–3 s al 4–5% sono transitori di acquisizione,
    e il tracciamento della deriva è dentro 1–2 BPM (verificato a 132).
  - **I guasti stanno ai bordi della gamma, e sono metà/doppio, non il tempo.**

  **Attribuzione corretta (stessa giornata, dopo una seconda misura).** In una
  prima lettura avevo dato la colpa a `kOctaveTooFast = 168`: **sbagliato**, quella
  costante sta solo in `BeatTracker.cpp` e questa probe è a livello di decoder, che
  non la vede nemmeno. Il fenomeno è il **livello metrico scelto in acquisizione**.
  Misurato con 20 semi per tempo (rapporto riportato/vero alla prima risposta):

  | BPM | giusto | a metà | al doppio |
  |---|---|---|---|
  | 50 / 60 | 2/20 | — | **18/20** |
  | 70 | 9/20 | — | 11/20 |
  | **80 → 160** | **20/20** | 0 | 0 |
  | 170 / 180 | 17/20 | 0 | 0 (3 "altro") |
  | 200 | 13/20 | **7/20** | 0 |

  Cioè: **la gamma dove vive quasi tutta la musica (80–160) è già pulita**, 20 su
  20. Quello che resta è raddoppio sotto i 70 e dimezzamento a 200 — esattamente
  l'ambiguità metà/doppio che l'item 1 ha dimostrato **indecidibile dall'audio**,
  non un bug da riparare. Il rimedio previsto è già in campo: TAP e ÷2/×2.
- [x] **Non inseguire questa con modifiche al tracker.** La prima ipotesi
  (isteresi sul confine dell'ottava) è stata scartata: il confine non è nel
  decoder e la gamma centrale non ha il problema. Tarare qui vorrebbe dire tarare
  sul rumore. Il lavoro che paga è l'**item 19**, che è un bug vero e riparabile.

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

| salto | delta | normale | con input-restart |
|---|---|---|---|
| 120 → 132 | +10% | 4.1 s | 0.5 s |
| 120 → 150 | +25% | 17.2 s | 0.8 s |
| **120 → 160** | +33% | **MAI** | 0.8 s |
| 100 → 160 | +60% | 27.8 s | 0.8 s |
| 160 → 100 | −38% | 15.6 s | 0.6 s |
| 120 → 90 | −25% | 10.7 s | 0.7 s |
| 90 → 120 | +33% | 34.7 s | 24.7 s |
| **140 → 75** | −46% | **MAI** (deriva a 150) | 0.8 s |
| 75 → 140 | +87% | 8.1 s | 0.8 s |

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

| banco | baseline | con watchdog |
|---|---|---|
| salto 120→160 | **MAI** (>182 s a 120,00) | **22,6 s** |
| resto della tabella salti | — | **invariata** |
| tempo costante, ogni BPM da 60 a 170 | — | **identica alla baseline** |

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

Vedi **`docs/HANDOFF_LOOP_DEBUG.md`**. Switch LOOP/PATTERN, banco `Assets/Loops/dance`, debug iPad (gracchiio, 48 kHz, swing oltre 18%, ecc.). Fuori scope finché resti su PATTERN.

---



## Chiuso

*(sposta qui gli item con data breve quando sono integrati)*

---



## Note per chi riprende

- Skill tempo: `.claude/skills/realtime-tempo/SKILL.md`  
- Skill parti: `.claude/skills/percussion-patterns/SKILL.md`  
- Tempo lento ~50 BPM + hat ottavi: item 1 — root cause confermata (ottava bloccata sugli ottavi). Due versioni di `BeatDecoder::observeDownbeatCadence` provate e insufficienti (soglia forte; poi probabilità continua con istogramma a tempo-indicizzato): la curva di downbeat della rete non distingue le due ipotesi su questo materiale per via della stessa ambiguità 1-vs-3 dell'item 2. Non riprendere con un altro tentativo isolato su pBeat/pDownbeat — serve una feature spettrale nuova o una validazione multi-brano estesa (vedi item 1 per i dettagli).  
- Battuta: `BeatTracker::alignBarFromVotes` / `notifyBarReentry`, `maybeDetectBarReentry`, `barLocked` (item 2). `VPTests --bar`. Restano `VPBar` su rete vera e l'ascolto del taglio.  
- Cambio brano a START on: `loadInternalTrack` senza reset tracker (item 3)  
- Drift guard: settings + mute in render; non mescolare con `BandDynamics::wantsSilence` (item 4)  
- Suddivisione: STRUMENTI `subAuto` / `sub4` / `sub8` / `sub16`; AUTO deve adattare 1/4↔1/8↔1/16 sull'ottava (item 6)  
- Swing: oggi knob `swingSlider`; deve diventare tasto ON/OFF in STRUMENTI (item 7); warp in `GrooveEngine::humanDelay` (`kFullSwingBeats`)  
- STOP: oggi `VirtualPercussionEngine::stop` + `percussion.silence()` immediato; deve diventare trillo shaker + fade (item 8)  
- PARTE: select custom `StyleSelect` + DINAMICA (`MainComponent`); AUTO prima voce, default motore MARCHA / `grooveAuto` off (item 12)
- UI tasto battuta: tap su «L'1 è QUI» sblocca senza nudge (`barControlNudgeOnTap`, item 13)
- NATURALE: `GrooveEngine::setShakerNatural`, tasto in STRUMENTI, default off (item 11)
- Clap gate (item 10): `tr.barTrusted` dal tracker (lucchetto o istogramma sul zero, muto in finestra di rientro). Ascolto render + brano OK; manca il taglio di due quarti.  
- Brano: `trackLoadButton` / `trackPlayButton` / `trackTransport`; waveform+seek in SETUP (`TrackWaveform`, item 14)  
- ÷2 / ×2: **rimossi** (item 15, 2026-09-03); ottava sempre auto in `BeatTracker`  
- Ottava automatica: si muove **solo se non sta suonando nulla** (item 17, `BeatTracker::updateAutoOctave`). Il livello si sceglie in acquisizione e si tiene; ÷2/×2 restano la via manuale anche a parte suonante. Regressione: `VPProbe --trace --live --mixer plain 168` deve finire ~167, non 84.
- Assestamento a volte lentissimo (fino a 30 s) sullo stesso materiale che di solito prende 2 s: item 18, aperto, **non** inseguirlo prima di sapere se esiste fuori dal banco (`VPProbe --sync`).
- Chiusura Codex PATTERN: parte tecnica completata; resta l'ascolto umano in **Standby A**. Loop WAV registrati: **Standby B** + `HANDOFF_LOOP_DEBUG.md`
- Guadagno automatico analisi (item 16): `kMakeupClipGuardPeak` in `VirtualPercussionEngine.cpp`, attenua solo sopra 0.90 di picco. Test veloce dedicato: `VPTests --octave` (non lanciare la suite intera per iterare qui). Full-suite gate e ascolto ancora da fare.
