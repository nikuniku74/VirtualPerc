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
    try { localStorage.setItem('vp-lang', l); } catch (e) {}
  }
  d.classList.add('js');
  set(want);
  document.addEventListener('click', function (e) {
    var a = e.target.closest && e.target.closest('[data-lang]');
    if (a) { e.preventDefault(); set(a.getAttribute('data-lang')); }
  });
})();
