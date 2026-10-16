# App Store Connect, passo passo (iPhone, iPad, Mac)

Scritto il 2026-10-16. I nomi dei menu sono quelli che conosco; Apple li ritocca,
quindi se un nome è leggermente diverso, cerca quello simile nella stessa zona.
I testi lunghi da incollare sono in `docs/store/listing.md` (blocchi
«English (U.S.)» e «Italiano»).

**Regola d'oro:** non serve compilare tutto subito. Per **TestFlight interno**
bastano le parti 1, 2 e 3. Il resto (testi, screenshot, privacy, età) serve solo
quando premerai «Invia per la revisione».

Apri <https://appstoreconnect.apple.com> → **Apps** → **Virtual Percussionist**.
A sinistra vedi un menu: lo chiamo «il menu a sinistra».

---

## Una app sola per iPhone, iPad e Mac

- **iPhone e iPad**: è la stessa app (universale). Non crei nulla di separato.
- **Mac**: non c'è una scheda separata né un build separato. La stessa app gira
  sui Mac con chip Apple (M1 e successivi) come «app per iPhone e iPad». È
  accesa di default: si controlla alla parte 4.
- Gli stessi acquisti funzionano su tutti e tre.

---

## Parte 1 - I tre prodotti (acquisti in-app)

Nel menu a sinistra cerca **Monetization** (Monetizzazione): dentro ci sono
**Subscriptions** (Abbonamenti) e **In-App Purchases** (Acquisti in-app).

### 1A. Gli abbonamenti (mensile e annuale)

1. **Subscriptions** → **Create** (crea gruppo di abbonamenti).
   - *Reference Name*: `Virtual Percussionist Pro`
2. Dentro il gruppo, aggiungi la **localizzazione del gruppo** (nome che vede il
   cliente): lingua English → *Subscription Group Display Name*:
   `Virtual Percussionist Pro`. Poi Italiano: lo stesso.
3. Dentro il gruppo, **Create Subscription** (+). Fai questo **due volte**:

| campo | MENSILE | ANNUALE |
|---|---|---|
| Reference Name | `Pro Monthly` | `Pro Yearly` |
| Product ID (**non si cambia più**) | `com.virtualpercussionist.app.pro.monthly` | `com.virtualpercussionist.app.pro.yearly` |
| Subscription Duration | **1 Month** | **1 Year** |

4. In **ciascuno** apri la pagina e compila, dall'alto:
   - **Subscription Prices** → **+** → scegli come paese base **United States**,
     prezzo **9,99 USD** (mensile) o **59,99 USD** (annuale). Apple calcola gli
     altri paesi: lascia fare.
   - Sempre lì, **+** accanto a «Subscription Prices» → **Create Introductory
     Offer** (offerta introduttiva): paesi = **tutti**, tipo = **Free**
     (gratuito), durata = **1 week** (1 settimana). Nessuna data di fine.
   - **Localization**: **+ Add Localization**.
     - English (U.S.): *Display Name* `Pro Monthly` (o `Pro Yearly`),
       *Description* `Full access, billed monthly` (o `billed yearly`).
     - Italiano: *Display Name* `Pro Mensile` (o `Pro Annuale`),
       *Description* `Accesso completo, addebito mensile` (o `annuale`).
   - **Review Information** → **Screenshot**: carica
     `docs/store/screenshots/en/iphone-6.9in/2-trial-offer.png` (è la
     schermata con il paywall). Le note puoi lasciarle vuote.
   - **Family Sharing**: lascia spento.
5. Nel gruppo, ordina i livelli: **annuale in alto**, mensile sotto.
6. Quando in alto dice **Ready to Submit** (pronto per l'invio) è fatto. Se dice
   **Missing Metadata**, clicca: elenca esattamente cosa manca.

### 1B. Il lifetime (acquisto singolo)

1. **In-App Purchases** → **+** (crea).
2. *Type*: **Non-Consumable**.
3. *Reference Name*: `Pro Lifetime`. *Product ID*:
   `com.virtualpercussionist.app.pro.lifetime`.
4. **Price Schedule** → prezzo base **United States 149,99 USD**.
5. **Availability**: tutti i paesi.
6. **App Store Localization**: English `Pro Lifetime` / `Full access, one payment`;
   Italiano `Pro Lifetime` / `Accesso completo, un solo pagamento`.
7. **Review Information**: stesso screenshot del paywall.
8. **Family Sharing**: a tua scelta (spento va bene).
9. Deve diventare **Ready to Submit**. **Niente offerta di prova**: non si può
   su un acquisto singolo.

### 1C. Un'impostazione da non toccare
Dentro *Subscriptions* c'è **Billing Grace Period** (tolleranza di fatturazione):
**lasciala spenta**.

---

## Parte 2 - Il contratto (altrimenti i prodotti non funzionano)

Menu in alto o a sinistra: **Business** (un tempo «Agreements, Tax, and
Banking»). Controlla che **Paid Apps** sia **Active**. Se non lo è: **Accept**,
poi inserisci **conto bancario** e **dati fiscali**, e la **dichiarazione di
operatore (trader) UE**. Serve il titolare dell'account.

---

## Parte 3 - Per TestFlight (basta questo, oltre alle parti 1 e 2)

Niente altro è obbligatorio per i tester interni. Quando carichi la build, apparirà
sotto **TestFlight**. Vedi `docs/TESTFLIGHT.md` (parti B e C).

---

## Parte 4 - Per la pubblicazione (si può fare dopo)

### 4A. Pricing and Availability (Prezzi e disponibilità)
- **Price**: **Free** (0). I prezzi stanno negli acquisti, non qui.
- **Availability**: tutti i paesi (o quelli che vuoi).
- **iPhone and iPad Apps on Apple Silicon Mac**: lascia **disponibile** (spunta
  accesa). Questa è la versione Mac.
- **iPhone and iPad Apps on Apple Vision Pro**: **togli la spunta** («Make this
  app available on Apple Vision Pro»).

### 4B. App Information
- *Name*: `Virtual Percussionist`
- *Subtitle*: EN `A percussionist that listens` / IT `Un percussionista che ascolta`
- *Category*: **Primary = Music**. Secondary: Productivity (o niente).
- *Privacy Policy URL*: l'indirizzo della tua pagina privacy (quando il sito è
  online; sul momento si può lasciare vuoto per TestFlight interno).
- *Content Rights* («l'app contiene contenuti di terzi?»): rispondi onestamente
  sui suoni (vedi `docs/APPSTORE.md`, inventario dei suoni).
- *Age Rating*: **Set Up** → il questionario → rispondi **None/Nessuno** a tutto:
  risulta **4+**.

### 4C. App Privacy
**App Privacy** → **Get Started** → alla domanda «raccogliete dati?» rispondi
**No, we do not collect data from this app** → **Publish**. (La privacy policy
sul sito dice la stessa cosa.)

### 4D. La pagina della versione 1.0
Nel menu a sinistra sotto **iOS App**: **1.0 Prepare for Submission**.
- **Screenshots**: carica da `docs/store/screenshots/<lingua>/`
  - **iPhone 6.9"** (obbligatorio): cartella `iphone-6.9in`, i 4 file.
  - **iPad 13"**: cartella `ipad-13in`, i 4 file. Valgono anche per il Mac.
  - Fallo per English **e** per Italiano.
- **Promotional Text**, **Description**, **Keywords**: da `listing.md`
  (blocchi English e Italiano; aggiungi la lingua dal menu a tendina in alto).
- **Support URL**: la tua pagina assistenza (obbligatorio per inviare).
- **Version**: `1.0.0`. **Copyright**: `© 2026 Nicola Marogna` (o la società).
- **What's New**: `First release.` / `Prima versione.`
- **Build**: dopo il caricamento da Xcode, **+** e scegli la build.
- **App Review Information**: nome, cognome, telefono, email; *Sign-in
  required*: **No**; *Notes*: il blocco «Note per la revisione» in `listing.md`.
- **In-App Purchases and Subscriptions** (sulla stessa pagina): **+** e
  aggiungi i tre prodotti (obbligatorio la prima volta).
- **Add for Review → Submit for Review**: solo quando tutto è pronto.

---

## Se qualcosa non va

- Un punto con un triangolo giallo/rosso: cliccalo, dice cosa manca.
- «Missing Metadata» su un prodotto: di solito manca lo screenshot, una
  localizzazione o il prezzo.
- I prodotti non compaiono nell'app di test: contratto Paid Apps non attivo, o i
  prodotti non sono ancora «Ready to Submit», o l'ID non è identico a quello
  nel progetto (`Source/Platform/Subscription.h`).
- Il nome «Virtual Percussionist» già preso: scegline uno simile (ad es.
  «Virtual Percussionist: AI Drummer») e cambia solo il nome nella scheda: il
  bundle ID e gli ID dei prodotti non cambiano.
