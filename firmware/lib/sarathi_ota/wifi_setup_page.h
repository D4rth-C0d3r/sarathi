// The captive portal's one page (ADR-0004 §2). Self-contained: no external scripts, fonts or
// images, since the phone has no internet while it's on the setup network.

#pragma once

namespace sarathi::ota {

inline constexpr char kWifiSetupPage[] = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>sārathi Wi-Fi setup</title>
<style>
:root{color-scheme:dark}
body{margin:0;font:18px/1.4 system-ui,sans-serif;background:#000;color:#fff}
main{max-width:420px;margin:0 auto;padding:24px 16px}
h1{font-size:24px;margin:0 0 4px}p{color:#c8c8c8;margin:0 0 20px}
label{display:block;margin:16px 0 6px;color:#c8c8c8}
input,select,button{width:100%;box-sizing:border-box;font:inherit;padding:12px;
border-radius:10px;border:1px solid #555;background:#1e1e1e;color:#fff}
button{margin-top:24px;background:#ffb000;color:#000;border:0;font-weight:600}
button:disabled{opacity:.5}
#status{margin-top:20px;min-height:2.8em}
.ok{color:#7cff7c}.err{color:#ff8a80}
</style></head><body><main>
<h1>Connect sārathi to Wi-Fi</h1>
<p>Choose the network the dash should use for updates.</p>
<form id="f">
<label for="pick">Network</label>
<select id="pick"><option value="">Scanning…</option></select>
<label for="ssid">Network name</label>
<input id="ssid" name="ssid" maxlength="32" autocomplete="off" required>
<label for="password">Password</label>
<input id="password" name="password" type="password" maxlength="64" autocomplete="off">
<button id="go">Connect</button>
</form>
<div id="status"></div>
<script>
const $=id=>document.getElementById(id);
function say(text,cls){const s=$('status');s.textContent=text;s.className=cls||''}
fetch('/networks').then(r=>r.json()).then(list=>{
  const pick=$('pick');pick.innerHTML='';
  const first=document.createElement('option');first.value='';
  first.textContent=list.length?'Choose a network':'No networks found; type one below';
  pick.appendChild(first);
  for(const n of list){const o=document.createElement('option');o.value=n;o.textContent=n;pick.appendChild(o)}
}).catch(()=>{});
$('pick').onchange=e=>{if(e.target.value)$('ssid').value=e.target.value};
$('f').onsubmit=e=>{
  e.preventDefault();$('go').disabled=true;say('Sending…');
  const body=new URLSearchParams({ssid:$('ssid').value,password:$('password').value});
  fetch('/join',{method:'POST',body}).then(r=>r.json()).then(r=>{
    if(!r.ok){say(r.error,'err');$('go').disabled=false;return}
    say('Joining '+$('ssid').value+'… If this page stops updating, check the dash.');poll();
  }).catch(()=>{say('The dash did not answer. Try again.','err');$('go').disabled=false});
};
function poll(){
  fetch('/status').then(r=>r.json()).then(s=>{
    if(s.phase==='joined'){say('Connected and saved. You can close this page.','ok');return}
    if(s.phase==='waiting'&&s.failed){say("Couldn't join "+s.ssid+'. Check the password and try again.','err');$('go').disabled=false;return}
    setTimeout(poll,1000);
  }).catch(()=>setTimeout(poll,1500));
}
</script></main></body></html>
)html";

}  // namespace sarathi::ota
