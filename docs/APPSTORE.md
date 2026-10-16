# App Store: trial di 7 giorni e abbonamento

Stato al 2026-10-15. Cosa c'è nel codice, cosa va fatto fuori dal codice, cosa
manca, cosa è sicuro e cosa no. **Non è mai stato provato un acquisto vero**: il
codice StoreKit compila ma gira solo con un ID Apple di prova (vedi «Come
provarlo»).

## 1. Cosa è stato fatto

| pezzo | dove |
|---|---|
| StoreKit 2 (prodotti, acquisto, ripristino, gestione, aggiornamenti in tempo reale) | `Source/Platform/StoreKitBridge.swift` |
| API C++ senza dipendenze Apple | `Source/Platform/Subscription.h`, `Subscription.mm` (iOS), `Subscription.cpp` (host: sempre «abbonato») |
| Schermate: offerta con prova, «sei dentro» (prova / abbonato / disdetto), caricamento, offline, in attesa di approvazione, errore | `Source/UI/PaywallOverlay.{h,cpp}` |
| Blocco di START e PLAY senza accesso; riapertura se l'abbonamento scade mentre suona | `MainComponent::requireAccess`, `onSubscriptionStatus` |
| Pulsante **PRO / TRIAL** nella testata delle impostazioni → pagina dell'abbonamento | `MainComponentLayout.cpp` (`accountButton`) |
| Prodotti per prove locali in Xcode | `Assets/StoreKit/VirtualPercussionist.storekit` |
| Privacy manifest | `Assets/PrivacyInfo.xcprivacy` (bundle iOS) |
| `ITSAppUsesNonExemptEncryption = NO`, numero di build (`VP_BUILD_NUMBER`) | `CMakeLists.txt` |
| Anteprime PNG di tutte le schermate (EN/IT, iPhone, iPad, orizzontale, chiaro/scuro) + 10 controlli | `scripts/paywall_shots.cpp`, target `VPPaywall` |

### Decisioni

- **La prova è l'offerta introduttiva gratuita di 1 settimana sull'abbonamento**,
  non un contatore nostro. Apple la lega all'ID Apple: cancellare l'app, spostare
  l'orologio o ripristinare un backup non regala un secondo trial. Niente server,
  niente account, niente dati: la scheda privacy resta «Dati non raccolti».
- **Solo transazioni `.verified`** (firmate da Apple) danno accesso. Un file
  locale o una risposta alterata non bastano.
- **Soft paywall**: l'app si apre, si vede tutto, START e PLAY aprono l'offerta.
  Piace a App Review (può vedere l'app) e non perde chi vuole solo guardare.
  Dopo lo splash l'offerta si apre da sola, una volta per avvio, se non c'è
  accesso. «Non ora» la chiude.
- **Si applica solo alle build Release** (`VP_PAYWALL_ENFORCED`): una Run di
  Debug sull'iPad da banco non passa dal paywall. Per vederlo in Debug va
  passato `-DVP_PAYWALL_DEBUG=ON` a `cmake` (`configure-ios.sh` non lo espone).
  Host/test/probe non hanno mai paywall: lo stub risponde «abbonato».
- **Prezzi decisi (2026-10-16)**: mensile **7,99 €**, annuale **49,99 €**
  (-48% rispetto a dodici mesi), prova di 7 giorni su entrambi, e **lifetime
  119,99 €** come acquisto singolo (*non-consumable*, senza prova).
  Scartato il settimanale. Nella pagina il prezzo **addebitato** è la cifra
  grande di ogni carta; nessun equivalente mensile più in evidenza (Apple lo
  rifiuta). L'app mostra sempre i prezzi che StoreKit restituisce, già
  localizzati: quelli del file `.storekit` servono solo alle prove locali. Va
  creato in App Store Connect anche il prodotto lifetime
  (`com.virtualpercussionist.app.pro.lifetime`).

### Uso gratuito e regali

- **Tu**: una Run da Xcode in Debug non ha paywall. Con la build dello store: un
  **codice offerta** gratuito sul lifetime per il tuo ID Apple, oppure
  TestFlight (acquisti di prova gratuiti, 90 giorni per build). Nessuna porta
  segreta nell'app.
- **Regali**: *Offer Codes* in App Store Connect (valgono anche per acquisti
  singoli come il lifetime; codici monouso con scadenza massima di 6 mesi,
  1 milione a trimestre, max 10 offerte attive). Verificare in App Store
  Connect se il prezzo di un codice sul lifetime può essere zero. Chi li riceve
  li riscatta da un link `apps.apple.com/redeem?...`, dalla scheda dell'App
  Store o **dentro l'app** dal link «Riscatta codice» della pagina
  dell'abbonamento (`AppStore.presentOfferCodeRedeemSheet`, aggiunto
  2026-10-16, mai provato con un codice vero). L'acquisto arriva come una
  normale transazione verificata.

## 2. Da fare in App Store Connect (non automatizzabile)

Testi pronti da incollare (EN/IT: descrizione, parole chiave, abbonamenti, note
per la revisione), informativa privacy e pagina di assistenza da pubblicare:
**`docs/store/`**. Contengono segnaposto `[SITO]`, `[EMAIL]`, `[NOME LEGALE]` e
l'informativa **non è stata letta da un legale**.

1. **Contratti**: *Paid Apps Agreement* firmato, banca e fiscalità compilate.
   Senza, i prodotti non si caricano neanche in sandbox.
2. **Dichiarazione da operatore (DSA, UE)**: l'Italia è UE, va dichiarato se sei
   «trader» (con nome, indirizzo, telefono, email pubblici). Se vendi un
   abbonamento a scopo commerciale lo sei quasi sempre.
3. **App**: bundle id `com.virtualpercussionist.app` registrato, nome
   «Virtual Percussionist» disponibile (se è già preso serve un nome diverso).
4. **Gruppo di abbonamenti** «Virtual Percussionist Pro» con due prodotti,
   id **esatti**:
   `com.virtualpercussionist.app.pro.monthly` (1 mese) e
   `com.virtualpercussionist.app.pro.yearly` (1 anno). In ciascuno:
   *Introductory Offer* → Free, 1 settimana, tutti i paesi; nome e descrizione
   in inglese e italiano; **screenshot per la revisione** (usa
   `en-1-offer-trial-phone.png`: è la schermata che Apple vuole vedere).
5. **Impostazioni app**: URL privacy policy, URL assistenza, categoria
   (Musica), classificazione età, copyright, *EULA* (si può usare quello
   standard di Apple, già linkato nell'app), disponibilità **tolta da Apple
   Vision** e lasciata su Mac (vedi `docs/PLATFORM.md`).
6. **Scheda privacy**: «Dati non raccolti». Il microfono è analizzato sul
   dispositivo e non esce. Se aggiungerai crash report o analytics di terzi,
   cambia.
7. **Note per la revisione** (modello più sotto): senza queste l'app viene
   rifiutata perché «non fa niente» a chi la prova senza una band.

## 3. Cosa manca prima di inviare

**Bloccanti**

- [ ] **Licenza JUCE** (decisione e acquisto sono tuoi). L'AGPL non è
  compatibile con l'App Store. Livelli letti il 2026-10-16: **Starter** gratis
  fino a 20.000 $ di ricavi, **Indie** 800 $ una tantum (o 40 $/mese) fino a
  300.000 $, **Pro** oltre. Una licenza JUCE 9 copre anche JUCE 8.0.15, che
  usiamo. **Il livello dipende da chi è l'intestatario**: persona (conta solo
  quello che ricava dall'uso di JUCE) o società (conta tutto il suo fatturato).
  Dettagli, consiglio e passi in `docs/store/juce-licence.md`.
- [ ] **URL reali**: `PaywallOverlay.cpp` (`kPrivacyUrl`) punta a
  `https://virtualpercussionist.app/privacy`, **un indirizzo che ho inventato**.
  Va sostituito con una pagina vera (e la stessa in App Store Connect). Se la
  pagina non esiste, è un rifiuto certo (guideline 3.1.2 e 5.1.1).
- [ ] **Origine dei suoni** (scoperto il 2026-10-16 leggendo `Assets/Percussion/ATTRIBUTION.md`,
  e `docs/LICENSES.md` è rimasto indietro): i **campioni di conga e shaker** non sono CC0
  ma tagliati da loop di SampleFocus («Standard Licensing - Royalty Free»), e il file
  stesso avverte che includerli in un'app distribuita può essere una
  ridistribuzione non consentita. Inoltre **`Assets/Hits/*.wav`** (absorb,
  cinematic_riser_boom, reggae_horn, riser_2, uplifter_fx, windchimes) **non hanno
  nessuna fonte né licenza scritta**. Sono i suoni centrali dell'app: o si
  documenta una licenza che consenta la ridistribuzione, o si sostituiscono con
  suoni CC0 (VCSL e i loop di Freesound già usati sono a posto).
- [x] **Schermata Licenze** (2026-10-16): link «Licenze» nella pagina
  dell'abbonamento (`PaywallOverlay::LicensesPage`) con l'attribuzione di
  BeatNet (CC BY 4.0), il testo MIT di ONNX Runtime, JUCE e i suoni CC0.
  **Va aggiornata** quando si risolve il punto sopra e per ogni nuova dipendenza.
- [x] **Un modo per provare l'app senza una band** (2026-10-16): premendo PLAY
  nelle impostazioni senza nessun brano caricato parte un brano di prova incluso
  (`Assets/Demo/demo-track.wav`, 34 s, 112 BPM, sintetico, generato da noi con
  `VPDemoTrack`, quindi senza licenza di terzi). **Cambia il comportamento di
  PLAY a vuoto**, che prima apriva il selettore file (CARICA lo apre ancora).
  Non ancora ascoltato né provato su dispositivo: vedi sotto.
- [ ] **Prove vere dello StoreKit** (sezione 5): acquisto, prova, ripristino,
  scadenza, rimborso, offline.
- [~] **Scheda dello store** (2026-10-16): screenshot in inglese e italiano pronti nelle
  misure giuste (iPhone 6,9" 1320x2868 e iPad 13" 2064x2752): console che segue il
  brano di prova, offerta con prova, impostazioni, stato abbonato, in
  `docs/store/screenshots/` (18 MB, **non committati** per non appesantire la
  storia di git; README nella cartella). Console e impostazioni vengono dal
  simulatore, offerta e stato sono renderizzati da `VPPaywall --store`: **i
  prezzi lì sono segnaposto**. Restano testi (pronti in `docs/store/`),
  parole chiave, URL assistenza e classificazione età in App Store Connect.
- [x] **Versione** `1.0.0` in CMake; `VP_BUILD_NUMBER` va aumentato a ogni invio.
- [x] **Icona dell'app** = il logo della splash (2026-10-16). Trovato e risolto:
  JUCE scriveva tutte le icone **con canale alfa** e App Store Connect rifiuta
  l'icona 1024 con alfa (ITMS-90717): `CMakeLists.txt` le appiattisce a ogni
  configure con Pillow (`pip3 install pillow`; altrimenti avvisa).
- [ ] **Registro delle build con Xcode 26 / SDK iOS 26** (da fine aprile 2026
  Apple accetta solo quelle). Qui è Xcode 26.6: ok.

**Importanti**

- [x] **Lingue: inglese e italiano** (2026-10-16). `Source/UI/Localization.h`:
  `vpui::L ("english", "italiano")` sceglie dalla lingua del dispositivo (e dalla
  lingua per-app di iOS: `CFBundleLocalizations` en/it + `en.lproj` e `it.lproj`
  con il permesso del microfono in `Assets/Localization`). Il testo italiano è
  quello che l'app aveva, parola per parola; l'inglese è nuovo. `Core/` non è
  stato toccato (test e probe cercano quei nomi): la UI li traduce in
  `vpui::styleName / kitName / barLabel`. Pannello debug e righe di log
  restano in italiano (non si vedono). **Da rivedere a occhio** con l'app in
  inglese: la lunghezza dei testi nei pulsanti stretti, e la scelta di
  «MARCH», «TAMBOURINE», «SHIFT THE 1». Resta da fare lo stesso per la scheda
  dello store (già bilingue in `docs/store/`).
- [ ] **`UIFileSharingEnabled`** è attivo di default: la cartella Documenti è
  visibile in File e si scrive `VirtualPercussionist-stopstart.log` (solo numeri
  di tempo). Utile al banco, inutile in un prodotto. **Per l'archivio dello
  store configura con `-DVP_FILE_SHARING=OFF`.** (Per il simulatore: `-DVP_STOREKIT=OFF`, senza Swift: il link Swift fallisce lì.) `UISupportsDocumentBrowser`
  resta com'è.
- [ ] **Audio in background** (`UIBackgroundModes audio`): Apple controlla che
  sia usato davvero. Lo è (l'app suona a schermo bloccato), dillo nelle note.
- [ ] **Privacy manifest**: `PrivacyInfo.xcprivacy` elenca le API «a motivo
  richiesto» che *ho ipotizzato* (timestamp file, UserDefaults, boot time,
  spazio disco). **Non è verificato**: dopo il primo Archive, *Product → Archive →
  Generate Privacy Report* dice cosa manca; ONNX Runtime e JUCE possono usarne
  altre.
- [x] **Sottomodulo JUCE sporco** (2026-10-16): `third_party/JUCE` ha modifiche
  non committate in `juce_Audio_ios.cpp` (sample rate dichiarati, niente HFP,
  nessun `jassert` sul primo callback in ritardo...). Il vecchio
  `third_party/juce_Audio_ios.patch` era fermo a una versione precedente (80
  righe contro 381). Ora la modifica attuale è salvata in
  **`third_party/juce-ios-audio.patch`** e `./scripts/apply-juce-patch.sh` la
  applica (o dice che c'è già) dopo un `git submodule update`. Da fare prima di
  una build da checkout pulito o su Xcode Cloud. In quel file c'è anche
  `JUCE_IOS_AUDIO_LOGGING 1 // VPDIAG temporaneo`: è solo `DBG`, quindi non ha
  effetto in Release, ma andrebbe rimesso a 0 quando la diagnosi è chiusa.
- [ ] **Modello `beatnet.onnx` addestrato su registrazioni tue di brani** (item 87
  del TODO). I pesi di partenza sono CC BY 4.0, ma la riaddestrazione su
  registrazioni di brani altrui è una questione di diritti da chiarire con chi
  se ne intende prima di venderlo. Non è una verifica che posso fare io.
- [ ] **Accessibilità**: la UI è disegnata a mano (JUCE). La pagina
  dell'abbonamento usa pulsanti veri con titolo, quindi VoiceOver li trova; la
  console no. Apple sta introducendo le «Accessibility Nutrition Labels»: non
  bloccanti oggi.

**Consigliati**

- TestFlight esterno prima del primo invio (serve la «Beta App Review»).
- Un sito di una pagina con assistenza e privacy.
- Programma Small Business (commissione 15% sotto 1 M$).
- Offerte dedicate (codici promo per i beta tester, offerta di ritorno per chi
  ha disdetto): StoreKit 2 le supporta, qui non sono cablate.

### Inventario dei suoni (2026-10-16, solo lettura: nessun suono è stato cambiato)

| cosa | file | origine | licenza |
|---|---|---|---|
| banco di loop registrati | `Assets/Loops/dance/*.wav` (12) | Freesound: nicpressley, Sycopation, shpira | **CC0**, documentato in `SOURCES.md` |
| clap, cembalo (tamburello) | `Assets/Percussion/clap*`, `cembalo_*` (9) | VCSL (Versilian) | **CC0** |
| conga | `open*`, `tumba*`, `slap*`, `slap_closed*`, `heel`, `toe*`, `muff`, `tapado*` (21) | tagliati dal loop SampleFocus «Salsa Congas Loop - Dance 3» (utente Dezz) | **SampleFocus Standard Licensing**: non CC0 |
| shaker | `shaker_down*`, `shaker_up*` (5) | tagliati da `soft-bright-shaker_128bpm.wav`; sito non scritto, ma Nicola dice che tutto viene da SampleFocus | SampleFocus Standard License, **conferma da raccogliere** |
| effetti | `Assets/Hits/*.wav` (6: absorb, reggae_horn, uplifter_fx, riser_2, windchimes, cinematic_riser_boom) | SampleFocus: `uplifter_fx` e `windchimes` lo dicono nei metadati; per gli altri quattro è la memoria di Nicola (2026-10-16), senza prova | SampleFocus Standard License, **conferma da raccogliere** |
| triangolo | (sintetizzato dal motore) | nostro | nostro |

Cosa dice SampleFocus (letta il 2026-10-16 da `samplefocus.com/license`,
versione «Last updated: July 21, 2026»; **mia lettura, non un parere legale**):
- Permette l'uso in «giochi, app e progetti multimediali», senza attribuzione,
  **«come parte di una nuova opera creativa»**.
- Vieta di ridistribuire il suono **da solo o in forma sostanzialmente uguale
  a quella scaricata**; di renderlo disponibile in un formato completo,
  scaricabile o **facilmente estraibile**; di includerlo in un **sample pack,
  libreria di suoni** o prodotto simile.
- Vieta di usare il materiale per **addestrare, affinare, validare, provare o
  misurare** qualsiasi modello di intelligenza artificiale / machine learning.

Come si applica qui, in chiaro:
- Le conga sono battute singole tagliate dal loop, filtrate e normalizzate,
  incorporate nell'app e **suonate come uno strumento a campioni**. Non è un
  brano finito che le contiene: è il suono stesso che l'app mette in vendita.
  Per questo il caso **non è chiaramente coperto**: può passare per «parte di una
  nuova opera» (l'app) oppure per «libreria di suoni» (uno strumento fatto di
  campioni). Non lo posso decidere io.
- Il divieto sull'AI: il tracker è un modello ML (BeatNet su ONNX) e i banchi
  del progetto fanno girare l'uscita delle percussioni, con questi campioni,
  attraverso quel modello (rientro dal ritorno acustico, prove con lo speaker).
  Non ho verificato se un test o un addestramento abbia davvero usato i campioni
  SampleFocus, ma la frase «validare, provare, misurare» è ampia: è da chiarire
  insieme al resto.

**Per tenerli serve una conferma scritta di SampleFocus** (bozza della mail in
`docs/store/samplefocus-email.md`). Finché non c'è, vale come non risolto.

Strade, da scegliere tu:
1. **Tenerli** solo se SampleFocus conferma per iscritto che questo uso (campioni
   incorporati come strumento di un'app a pagamento) è ammesso. **Scelta di
   Nicola del 2026-10-16: tenerli tutti** (conga, shaker, sei effetti), senza
   dichiarare le fonti nelle schermate dell'app e senza gli URL delle singole
   pagine (non recuperabili). La conferma scritta di SampleFocus **non è stata
   chiesta**: il rischio è accettato da Nicola, e resta che la licenza non copre
   chiaramente il caso (vedi sopra). Va scritto in
   `ATTRIBUTION.md` con data e link.
2. **Tornare ai CC0** per conga e shaker: VCSL (come prima del 2026-09-28, con
   `scripts/prepare_vcsl_samples.py`) oppure ritagliare le battute dai loop
   Freesound CC0 che ci sono già, con la stessa tecnica di
   `prepare_loop_congas.py` / `prepare_loop_shaker.py`. **Cambia il suono**: va
   riascoltato e rimisurato (nessuna misura di timing da rifare se l'attacco è
   compensato come oggi, ma l'ascolto sì).
3. **Effetti (`Assets/Hits`)**: sono extra («ABSORB», «HORN»...), 9,5 MB. Togliere
   quelli senza fonte è il modo più rapido: la sezione sparisce dall'app o si
   riduce ai due/quattro con licenza chiara.

## 4. Sicurezza e privacy: cosa ho trovato

| gravità | cosa | stato |
|---|---|---|
| nessuna rete | `JUCE_USE_CURL=0`, `JUCE_WEB_BROWSER=0`, nessun URL nel codice a parte i due link della pagina. Nulla esce dal dispositivo. | ok |
| microfono | testo d'uso presente e chiaro (`NSMicrophoneUsageDescription`); audio analizzato e scartato | ok |
| paywall solo sul client | chi ha un iPad con jailbreak e un debugger può togliere il controllo. Senza server non si impedisce. Accettabile per un'app a 5 €/mese; se diventasse un problema: `AppTransaction`/`AppStore.sync` + un piccolo server che valida le notifiche | accettato |
| orologio del dispositivo | la scadenza è confrontata con l'ora locale (`Date()` in `StoreKitBridge.swift`, e StoreKit stesso offline). Tornare indietro con l'orologio **senza rete** può prolungare una prova o un periodo già scaduto, finché il dispositivo non si riconnette e StoreKit rilegge. La prova non si può invece rinnovare (la lega Apple all'ID). Rischio basso, nessuna difesa senza server | accettato |
| scadenza offline | `Transaction.currentEntitlements` funziona senza rete finché la transazione firmata in cache non scade | ok, da provare |
| periodo di tolleranza di fatturazione | non gestito esplicitamente: se lo attivi in App Store Connect, chi è in tolleranza risulta *non abbonato* finché Apple non riprova. **Tienilo spento** o aggiungi la lettura di `subscription.status` | aperto |
| variabile `VP_BEAT_MODEL` | `ModelLocator.cpp` carica un modello da questo percorso se esiste. In un'app iOS nessuno può impostarla (sandbox). Nessun rischio sul dispositivo | ok |
| percorsi relativi del modello | cerca `Assets/Models/beatnet.onnx` anche relativo alla cartella corrente; in iOS il modello è incorporato (`VpBeatModelData`) e vince | ok |
| `FILE_SHARING_ENABLED` | vedi sopra: espone la cartella Documenti | da spegnere per lo store |
| `CFBundleVersion` | era `0.1.0`: App Store Connect rifiuta due caricamenti con lo stesso valore | corretto (`VP_BUILD_NUMBER`) |
| firma | `Apple Development` fisso nel CMake: per l'Archive va bene con firma automatica (Xcode usa il certificato di distribuzione all'export) | da verificare al primo Archive |
| segreti nel repo | nessuna chiave, nessun token. Il Team ID (`28H5MJ7244`) non è segreto | ok |

## 5. Come provarlo

**Prove locali in Xcode (senza account di prova):**

```bash
cmake -S . -B build-ios -DVP_PAYWALL_DEBUG=ON   # paywall attivo anche in Debug
```

Poi in Xcode: *Product → Scheme → Edit Scheme → Run → Options → StoreKit
Configuration* → `Assets/StoreKit/VirtualPercussionist.storekit`. La scelta va
rifatta se rigeneri il progetto (la schema è generata da CMake). Con la finestra
*Debug → StoreKit → Manage Transactions* si accelera il tempo (la settimana di
prova passa in minuti), si rimborsa, si scade, si simula un errore.

**Prove con ID Apple di prova (sandbox):** crea un *Sandbox Apple Account* in
App Store Connect → Utenti e accessi → Sandbox, poi su iPad *Impostazioni →
App Store → Account sandbox*. In sandbox l'abbonamento mensile dura 5 minuti.

**Da provare, con il risultato atteso:**

1. Primo avvio, nessun abbonamento: dopo lo splash si apre l'offerta con
   «Prova gratis per 7 giorni», piano annuale selezionato, prezzo e testo di
   rinnovo visibili. START non parte.
2. «Non ora»: console visibile; START riapre l'offerta; PLAY su un brano
   caricato idem.
3. Acquisto con prova: foglio di pagamento Apple che dice «7 giorni gratis»; a
   fine acquisto l'offerta si chiude, START parte, il pulsante nelle impostazioni
   dice **TRIAL** e la pagina mostra i giorni rimasti.
4. Stesso ID, cancella e reinstalla l'app: **nessuna seconda prova** («Subscribe»
   senza «gratis»).
5. Scadenza durante la riproduzione: l'app si ferma e riapre l'offerta con
   «Riprendi da dove avevi lasciato».
6. Ripristina acquisti su un secondo dispositivo: si sblocca.
7. Modalità aereo con abbonamento valido: l'app resta sbloccata. Modalità aereo
   senza abbonamento: «App Store non raggiungibile», *Riprova*.
8. Mac (Designed for iPad): offerta, acquisto, ripristino.
9. Ruota l'iPad e prova Split View: la pagina si riadatta.
10. Gestione abbonamento: apre il foglio di Apple; disdire fa passare la pagina
    a «Termina il …».

## 6. Note per la revisione (da incollare in App Store Connect)

> Virtual Percussionist is a live percussion companion: it listens to a band
> (microphone, line input from a mixer, or a track loaded in the app) and plays
> shaker, congas and other percussion in time. To try it without a band: tap
> START with the built-in demo track loaded (or PLAY on the loaded track).
> The microphone is analysed on the device only and nothing is recorded or
> sent. Background audio is used so a live set keeps playing when the iPad
> locks. Subscription: 7-day free trial, then monthly or yearly; both are in the
> «Virtual Percussionist Pro» group. The subscription page opens from START when
> there is no active subscription, and from SETUP → PRO. Restore Purchases, Terms
> of Use and Privacy Policy are on that page.

## 7. Verifiche: cosa è provato e cosa no

**Provato su iPhone 15 con Xcode e il file `.storekit` (2026-10-16):** il foglio di
pagamento compare, l'acquisto mensile con prova va a buon fine, il pulsante in
SETUP passa a TRIAL e la pagina «Tutto pronto» mostra piano e giorni rimasti. Trovato:
i giorni rimasti erano **8 su una prova di 7** (orologio del telefono indietro
rispetto al Mac, che in Xcode fa da negozio); ora il conteggio non supera mai la
durata della prova (`daysLeft(..., cap)`, con test).

**Provato (Mac, 2026-10-15):**

- `VPPaywall`: 10 controlli (testo del pulsante con/senza prova, pagina di stato,
  italiano, giorni rimasti arrotondati per eccesso, occupato) passano; 22
  schermate generate e riviste (iPhone, iPad, orizzontale, chiaro/scuro, EN/IT).
- L'app host (`Virtual Percussionist`) compila e collega con lo stub (sempre
  «abbonato»), quindi nessun cambiamento per il banco e per i test.
- **Build iOS Release senza firma** (`xcodebuild -sdk iphoneos`, Swift + ObjC++ +
  C++ nello stesso target): *BUILD SUCCEEDED*. Nel bundle: `PrivacyInfo.xcprivacy`,
  `CFBundleVersion = 1`, `ITSAppUsesNonExemptEncryption = false` (ricompilato dopo la correzione), `VPStore` collegato, e il paywall attivo (Release).
  Problema trovato e risolto lungo la strada: i flag comuni di JUCE (`-flto` e le
  opzioni `-W` di clang) venivano passati anche a `swiftc`, che li rifiuta
  (`OTHER_SWIFT_FLAGS` in `CMakeLists.txt`); e `PLIST_TO_MERGE` vuole un
  `<plist><dict>…</dict></plist>` intero, altrimenti la chiave sparisce in silenzio.

**Non provato:**

- Nessun acquisto, nessuna prova, nessun ripristino: StoreKit non gira fuori da
  un dispositivo o da Xcode con il file `.storekit`.
- Il file `.storekit` è scritto a mano dal formato noto; **non l'ho aperto in
  Xcode**. Se Xcode lo rifiuta, si crea da *New → StoreKit Configuration File*
  con gli stessi due id.
- L'aspetto sui dispositivi veri (le schermate sono disegnate col font del Mac:
  Futura Condensed ecc. esistono anche su iOS, ma i margini dell'isola
  dinamica sono simulati con 59 pt).
- La build iOS firmata, l'Archive e l'installazione su un dispositivo.
