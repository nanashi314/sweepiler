// Cross-origin isolation on hosts that cannot send headers (GitHub Pages).
// Loaded by the page, this file registers itself as a service worker that adds
// COOP and COEP to every response, then reloads the page once so the worker
// serves it. SharedArrayBuffer (and so WebAssembly threads) needs both.
if(typeof window==='undefined') {
  self.addEventListener('install',function(){self.skipWaiting();});
  self.addEventListener('activate',function(e){e.waitUntil(self.clients.claim());});
  self.addEventListener('fetch',function(e){
    var r=e.request;
    if(r.cache==='only-if-cached' && r.mode!=='same-origin') return;
    e.respondWith(fetch(r).then(function(res){
      if(res.status===0) return res;
      var h=new Headers(res.headers);
      h.set('Cross-Origin-Embedder-Policy','require-corp');
      h.set('Cross-Origin-Opener-Policy','same-origin');
      h.set('Cross-Origin-Resource-Policy','cross-origin');
      return new Response(res.body,{status:res.status,statusText:res.statusText,headers:h});
    }));
  });
} else if(window.crossOriginIsolated) {
  sessionStorage.removeItem('coiReloaded');
} else if(window.isSecureContext && navigator.serviceWorker) {
  navigator.serviceWorker.register(document.currentScript.src).then(function(){
    return navigator.serviceWorker.ready;
  }).then(function(){
    // Once per tab session, so a browser that refuses isolation does not reload forever.
    if(!sessionStorage.getItem('coiReloaded')) {sessionStorage.setItem('coiReloaded','1'); location.reload();}
    else {var s=document.getElementById('status'); if(s) s.textContent='THIS BROWSER DID NOT ALLOW CROSS-ORIGIN ISOLATION.\nSWEEPILER NEEDS SHAREDARRAYBUFFER (WEBASSEMBLY THREADS).';}
  });
}
