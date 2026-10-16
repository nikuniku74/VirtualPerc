# Sito: dati e pubblicazione (in sospeso)

Stato 2026-10-16: **niente è stato pubblicato.** Il sito statico è in `site/`
(EN/IT) e `scripts/build-site.sh` ne fa la copia con i dati di contatto.

Dati indicati da Nicola, **non ancora confermati** (da riconfermare prima di
pubblicare):

- Nome editore: Nicola Marogna
- Email di assistenza e privacy: info@ritmodigitale.com
- Indirizzo postale: **non indicato** (per ora è stata data un'email; la pagina
  privacy lo richiede come identificazione del titolare - da decidere quale)

Da decidere: **dove pubblicare** (GitHub Pages a `nikuniku74.github.io/...`
oppure un dominio proprio con hosting gratuito) e se mostrare il nome utente
GitHub nell'indirizzo. Quando è deciso: `./scripts/build-site.sh <cartella>
"<nome>" "<indirizzo>" "<email>"`, poi pubblicare la cartella e mettere
l'indirizzo vero della privacy in `kPrivacyUrl` (`Source/UI/PaywallOverlay.cpp`).
