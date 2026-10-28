# Dal repo a TestFlight (account «ritmodigitale») e la tua versione FULL

Aggiornamento 2026-10-27: Archive ed esportazione App Store riusciti per
Store 1.0.0 (3), team Ritmo Digitale `C2APL6E5HJ`. L’IPA esportata ha firma di
distribuzione, accesso protetto, condivisione file spenta e manifest privacy.
Caricamento ed elaborazione riusciti il 27/10/2026: build 3 visibile in
TestFlight. Invio alla revisione riuscito il 27/10/2026 alle 13:11 (ora mostrata
da App Store Connect): stato «In attesa di verifica» per tutti e cinque gli
elementi, app iOS 1.0 / build 1.0.0 (3), lifetime, gruppo Pro, mensile e annuale.
ID invio: `91d8556a-9546-4483-a33e-24bb92ea6c33`. Il rilascio pubblico resta
manuale dopo l’approvazione. Avviso non bloccante già comunicato: manca il dSYM
dell’app, quindi i crash di questa build potrebbero non avere nomi di funzione leggibili.

Scheda Store aggiornata in italiano e inglese (Regno Unito); selezionata la
build 3. Caricati 38 screenshot con il nuovo marchio: per ciascuna lingua,
7 iPhone 6,3", 5 iPhone 6,9" e 7 iPad 13". Prima dell’invio corretta una
incongruenza dei file: i 40 export con nome `.png` contenevano JPEG; ricodificati
in PNG reali e sostituiti. La console iPad italiana è stata caricata dal JPEG
originale con estensione corretta (`1-console-it-ipad.jpg`), senza ricompressione,
dopo un ulteriore stallo di elaborazione. Verificate le 38 miniature elaborate
sui server Apple e superata la validazione di invio. Gli altri formati iPhone/iPad
ereditano le risorse correnti, senza vecchi set specifici. I materiali Mac
locali non sono screenshot di una versione macOS nativa e non sono stati caricati
come tali. FULL resta esclusa da App Store e TestFlight.

Creato il prodotto non consumabile `com.virtualpercussionist.app.pro.lifetime`
(ID Apple `6827497713`), con base Italia 149,99 €, localizzazioni IT/en-GB e
schermata di verifica elaborata e accettata nell’invio. La precedente bozza con ID prodotto `632353` resta
inutilizzata. Nessuna transazione reale o sandbox è stata eseguita in questo
passaggio; lo screenshot è un rendering dell’interfaccia, dichiarato nelle note.

Mensile e annuale: disponibilità nei 175 territori salvata dopo esplicita
conferma del titolare; annuale con pagamento anticipato dell’intero anno, senza
attivare la nuova opzione di impegno annuale con rate mensili. Confermate le
offerte gratuite di una settimana, già presenti dal 16/10 senza data di fine.
Corretti nome/descrizione inglesi scambiati dei due piani e aggiunte le
localizzazioni IT/en-GB del gruppo Pro. Dichiarazione sui contenuti di terzi
allineata alla conferma dei diritti JUCE/SampleFocus fornita dal titolare;
i documenti di licenza non sono stati esaminati in questo passaggio.

Due build, due cartelle, due identità, che non si mescolano:

| | store / TestFlight | tua versione FULL |
|---|---|---|
| cartella | `build-ios-store` | `build-ios-full` |
| bundle ID | `com.virtualpercussionist.app` | `com.virtualpercussionist.app.full` |
| nome sotto l'icona | Virtual Percussionist | Virtual Percussionist FULL |
| paywall | sì (StoreKit, prova, piani) | **no** (`VP_UNLOCKED=ON`, il pulsante dice FULL) |
| va su App Store / TestFlight | **sì** | **mai** |

Avendo bundle ID diversi puoi avere sullo stesso telefono sia la versione di
TestFlight sia la FULL, senza che una sostituisca l'altra.

---

## Parte A - Account e App Store Connect (una volta sola)

**A1. Capisci che account è «ritmodigitale».** Su <https://developer.apple.com/account>
→ *Membership details*: annota **Team ID** (10 caratteri), **tipo** (Individual o
Organization) e il **nome che comparirà come venditore**. Il progetto oggi è
firmato per lo Store dal team `C2APL6E5HJ` («Ritmo Digitale di Stefano Mura»),
verificato nel portale Apple il 27/10/2026. La firma locale FULL può usare un
altro team e non determina il venditore dello Store. Il tuo Apple ID deve essere
*Account Holder* o *Admin* per creare l'app.

**A2. Contratti, banca e fiscalità.** <https://appstoreconnect.apple.com> →
*Business* (già «Agreements, Tax, and Banking»): accetta il **Paid Apps
Agreement**, inserisci conto bancario e dati fiscali, e la dichiarazione di
**operatore (trader, DSA)** per l'UE. Senza questo i prodotti in-app non si
caricano e non si può vendere. Se l'account è una società serve chi ha il ruolo
di Account Holder.

**A3. Registra il Bundle ID.** developer.apple.com → *Certificates, Identifiers &
Profiles* → *Identifiers* → **+** → *App IDs* → *App* → descrizione «Virtual
Percussionist», **Explicit** `com.virtualpercussionist.app`. Capability:
**In-App Purchase** (acceso). Se l'ID è già preso da qualcun altro non si può
usare: cambialo in `CMakeLists.txt` (`VP_BUNDLE_ID`); gli ID dei prodotti
in-app sono indipendenti.

**A4. Crea l'app.** App Store Connect → *Apps* → **+** → *New App*: piattaforma
iOS, **Nome** «Virtual Percussionist» (deve essere libero; max 30 caratteri),
lingua principale, **Bundle ID** quello di A3, **SKU** a piacere (es.
`vp-ios-1`), accesso *Full Access*.

**A5. Crea i prodotti in-app** (app → *Monetization*):
1. *Subscriptions* → **Create group** «Virtual Percussionist Pro». Dentro:
   - `com.virtualpercussionist.app.pro.monthly`, durata 1 mese, prezzo base
     **9,99 $**.
   - `com.virtualpercussionist.app.pro.yearly`, durata 1 anno, prezzo **59,99 $**.
   - Per ognuno: *Subscription Prices*, *Localization* (EN e IT: nomi e
     descrizioni in `docs/store/listing.md`), e **Introductory Offer**:
     *Free*, **1 settimana**, tutti i paesi, nuovi abbonati.
   - **Review Screenshot** (richiesto): `docs/store/screenshots/en/iphone-6.9in/2-trial-offer.png`.
   - Metti l'annuale sopra il mensile nel gruppo.
2. *In-App Purchases* → **Create** → tipo **Non-Consumable**: ID
   `com.virtualpercussionist.app.pro.lifetime`, prezzo **149,99 $**,
   localizzazioni, screenshot di revisione. **Nessuna offerta di prova**.
3. Tutti devono arrivare allo stato **Ready to Submit** (metadati completi).
   Probabilmente è quello che serve perché compaiano nelle build di TestFlight;
   **non l'ho verificato**.
4. Tieni **spenta** la tolleranza di fatturazione (*Billing Grace Period*).

(Le descrizioni, le parole chiave, la privacy e il resto della scheda servono per
la pubblicazione, **non** per TestFlight interno.)

---

## Parte B - Il build per lo store e l'Archive

**B1. Strumenti.** Xcode 26, `cmake`, e `pip3 install pillow` (serve per togliere
il canale alfa dalle icone: senza, App Store Connect rifiuta l'icona).

**B2. Configura** (dalla cartella del progetto; sostituisci `TEAMID` con quello
di A1 e `1` con un numero **che sale a ogni caricamento**):

Per la release corrente usare `./scripts/configure-appstore.sh NUMERO`, che
isola la configurazione in `build-ios-store`, imposta entrambe le proprietà
del team Ritmo Digitale e mantiene `VP_APPSTORE_RELEASE=ON`. I comandi manuali
qui sotto descrivono il flusso generale precedente; non sostituiscono questo
controllo di release.

```bash
cmake -S . -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DVP_ENABLE_RECORDED_LOOPS=ON -DVP_USE_SIGNALSMITH=OFF \
  -DVP_DEVELOPMENT_TEAM=TEAMID -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=TEAMID \
  -DVP_UNLOCKED=OFF -DVP_FILE_SHARING=OFF -DVP_PAYWALL_DEBUG=OFF -DVP_STOREKIT=ON \
  -DVP_BUNDLE_ID=com.virtualpercussionist.app -DVP_BUILD_NUMBER=1
```

**B3. Controlla che sia davvero la build dello store** (devono tornare `OFF` e
`ON`):

```bash
grep -E "VP_UNLOCKED|VP_STOREKIT|VP_FILE_SHARING" build-ios/CMakeCache.txt
```

**B4. Archive in Xcode.**
1. `open build-ios/VirtualPercussionist.xcodeproj`
2. In alto, scheme **VirtualPercussionist**, destinazione **Any iOS Device (arm64)**.
3. Target → *Signing & Capabilities*: **Automatically manage signing** acceso e
   **Team = ritmodigitale**. (Il progetto è già impostato così.)
4. Menu **Product → Archive**. Si apre *Organizer*.

Alternativa da terminale (stesso risultato, l'archivio compare in Organizer):

```bash
xcodebuild archive -project build-ios/VirtualPercussionist.xcodeproj \
  -scheme VirtualPercussionist -configuration Release \
  -destination 'generic/platform=iOS' \
  -archivePath ~/Desktop/VirtualPercussionist.xcarchive -allowProvisioningUpdates
```

**B5. Prima di caricare, controlla l'archivio** (Organizer → tasto destro →
*Show in Finder* → *Show Package Contents* → `Products/Applications/`):

```bash
plutil -p "…/Virtual Percussionist.app/Info.plist" | grep -E "CFBundleIdentifier|CFBundleVersion|ITSApp|FileSharing"
```

Atteso: bundle `com.virtualpercussionist.app`, `CFBundleVersion` = il numero
scelto, `ITSAppUsesNonExemptEncryption => false`, **nessun** `UIFileSharingEnabled`.
Dentro l'app deve esserci `PrivacyInfo.xcprivacy`.

**B6. Carica.** In Organizer: **Distribute App → App Store Connect → Upload**,
firma automatica, avanti fino alla fine. (Prima puoi fare *Validate App*.)
Il manifest della privacy è stato scritto a mano: se Apple segnala API mancanti,
l'avviso lo dice; aggiungile in `Assets/PrivacyInfo.xcprivacy`.

**B7. Aspetta l'elaborazione** (da 10 minuti a un'ora). Arriva una email. La
build compare in App Store Connect → app → **TestFlight**.

---

## Parte C - TestFlight interno (solo tu e il tuo team, senza revisione Apple)

1. App Store Connect → *Users and Access*: i tester interni devono essere utenti
   dell'account (ruolo qualunque con accesso all'app); tu ci sei già. Fino a 100.
2. App → **TestFlight** → *Internal Testing* → **+** → nome gruppo (es. «Io») →
   aggiungi te stesso → **abilita la build** appena elaborata.
3. Sul telefono: installa **TestFlight** dall'App Store, apri l'invito (email o
   app), **Installa**.
4. Prova (il pagamento è di prova e gratuito: sul foglio compare «[Environment:
   Sandbox]»): offerta con prova, acquisto del mensile e dell'annuale,
   **lifetime** («Accesso lifetime», START parte), scadenza, ripristino,
   reinstallazione (nessuna seconda prova), offline, «Riscatta codice», il
   popup di benvenuto, il brano di prova.
5. Ogni nuovo caricamento: riconfigura con `-DVP_BUILD_NUMBER=2`, `3`… e rifai
   l'Archive. Una build di TestFlight scade dopo **90 giorni**.

**Per tester esterni** serve in più: *Beta App Review* di Apple, descrizione di
test e email di contatto, **il sito con la privacy online** e l'indirizzo vero
in `kPrivacyUrl` (`Source/UI/PaywallOverlay.cpp`), la **licenza JUCE** a posto.

---

## Parte D - La tua versione FULL, sui tuoi dispositivi

Aggiornamento27/10/2026: FULL1.0.0(2) riallineata a Source/ e marchio della
Store1.0.0(3), compilata/firmata e installata. Aggiornare la Store non aggiorna
FULL, e viceversa: i due bundle sono distinti. Chiudere l’altra variante durante
le prove audio. Il titolare, riprovando BRANO, conferma «Ora è più continua». Non è una
garanzia di assenza di ogni interruzione.


Non passa da TestFlight né da App Store: la installi tu da Xcode.

**D1. Configura** (cartella separata; stesso `TEAMID`):

```bash
cmake -S . -B build-ios-full -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DVP_ENABLE_RECORDED_LOOPS=ON -DVP_USE_SIGNALSMITH=OFF \
  -DVP_DEVELOPMENT_TEAM=TEAMID -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=TEAMID \
  -DVP_UNLOCKED=ON \
  -DVP_BUNDLE_ID=com.virtualpercussionist.app.full "-DVP_PRODUCT_NAME=Virtual Percussionist FULL"
```

(Comparirà un avviso: «VP_UNLOCKED=ON: this build has no paywall». È voluto.)

**D2. Installa.**
1. `open build-ios-full/VirtualPercussionist.xcodeproj`
2. Collega l'iPhone o l'iPad, sbloccalo, «Autorizza questo computer».
3. Scheme **VirtualPercussionist**, destinazione il tuo dispositivo.
4. *Product → Scheme → Edit Scheme → Run → Build Configuration → **Release*** (la
   più veloce, quella da palco).
5. Team = ritmodigitale (se Xcode lo chiede), **Run**.
6. Sul dispositivo, alla prima volta: *Impostazioni → Generali → VPN e gestione
   dispositivo* → fidati dello sviluppatore, se compare.

Compare un'icona «Virtual Percussionist FULL» accanto a quella di TestFlight;
nelle impostazioni dell'app il pulsante dice **FULL**. La build da Xcode su un
account a pagamento dura un anno.

Se all'avvio appare per un istante il vecchio logo nel riquadro scuro, prova
prima una nuova installazione in-place con ⌘R della FULL compilata pulita.
Il 28/10 il progetto ha ricevuto `BrandLaunchScreen` e `BrandLaunchLogo` nel
catalogo risorse: la build locale pulita li contiene e non include più il vecchio
storyboard. Se l'iPad conserva comunque la schermata precedente, Apple TN3118
richiede di rimuovere l'app e reinstallarla; prima salva gli eventuali file e
le impostazioni locali della FULL, che la rimozione cancella.

**D3. Regole.** Non archiviare mai `build-ios-full`. Per la versione dello
store si usa **solo** `build-ios-store`, configurato con
`./scripts/configure-appstore.sh NUMERO`. Se vuoi la versione dello store gratis sul
tuo telefono, usa TestFlight (acquisti di prova) o, dopo la pubblicazione, un
**codice offerta** sul lifetime.

---

## Cosa non so e come scoprirlo

- Se «ritmodigitale» è lo stesso team di «Stackhouse» o un altro → A1.
- Se i prodotti compaiono in TestFlight prima di essere approvati → lo si vede al
  primo giro; se non compaiono, di solito manca un metadato (screenshot di
  revisione, localizzazione) o il contratto Paid Apps.
- Se l'Archive passa al primo colpo → lo dirà Xcode (firma, privacy manifest,
  librerie ONNX).
