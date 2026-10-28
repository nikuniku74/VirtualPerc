// Shows the page in the device language (Italian or English); the switch links
// override it. With JavaScript off, both languages are on the page.
(function () {
  var d = document.documentElement;
  var saved = null;
  try { saved = localStorage.getItem('vp-lang'); } catch (e) {}
  var want = saved || ((navigator.language || 'en').toLowerCase().indexOf('it') === 0 ? 'it' : 'en');
  function set(l) {
    d.classList.remove('lang-en', 'lang-it');
    d.classList.add('lang-' + l);
    d.lang = l;
    var title = document.querySelector('title[data-' + l + ']');
    var description = document.querySelector('meta[name="description"][data-' + l + ']');
    if (title) document.title = title.getAttribute('data-' + l);
    if (description) description.content = description.getAttribute('data-' + l);
    var ogTitle = document.querySelector('meta[property="og:title"]');
    var ogDescription = document.querySelector('meta[property="og:description"]');
    if (title && ogTitle) ogTitle.content = document.title;
    if (description && ogDescription) ogDescription.content = description.content;
    try { localStorage.setItem('vp-lang', l); } catch (e) {}
  }
  d.classList.add('js');
  set(want);
  document.addEventListener('click', function (e) {
    var a = e.target.closest && e.target.closest('[data-lang]');
    if (a) { e.preventDefault(); set(a.getAttribute('data-lang')); }
  });
})();
