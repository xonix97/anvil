// Embedded UI for Anvil (served to WebView2 via NavigateToString).
// Flat design, no gradients. Agent loop runs in JS; native FS/command tools
// are exposed by the C++ host through window.chrome.webview messaging.
#pragma once

static const char* kAppHtml = R"HTMLDOC(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>Anvil</title>
<style>
  :root{--bg:#0f1115;--panel:#171a21;--panel2:#1e222b;--line:#2a2f3a;--text:#f2f4f8;--muted:#8b93a4;--accent:#ff6b35;--ok:#2dd4a7;--mono:'Cascadia Code',Consolas,monospace;}
  *{margin:0;padding:0;box-sizing:border-box;}
  html,body{height:100%;}
  body{font-family:'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--text);display:flex;flex-direction:column;overflow:hidden;}
  .titlebar{height:38px;background:var(--panel);border-bottom:2px solid var(--line);display:flex;align-items:center;padding:0 12px;gap:10px;-webkit-app-region:drag;}
  .titlebar .logo{width:20px;height:20px;border-radius:5px;background:var(--accent);display:grid;place-items:center;font-size:12px;}
  .titlebar b{font-size:14px;letter-spacing:.5px;}
  .titlebar .sp{flex:1;}
  .titlebar button{-webkit-app-region:no-drag;background:var(--panel2);border:1px solid var(--line);color:var(--text);border-radius:6px;padding:5px 10px;font-size:12px;cursor:pointer;}
  .titlebar button:hover{border-color:var(--accent);}
  .main{flex:1;display:flex;min-height:0;}
  .sidebar{width:230px;background:var(--panel);border-right:2px solid var(--line);display:flex;flex-direction:column;}
  .side-head{padding:10px 12px;font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:1px;display:flex;justify-content:space-between;align-items:center;}
  .tree{flex:1;overflow:auto;padding:4px 0;}
  .tree .f{padding:5px 14px;font-size:13px;cursor:pointer;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:#cdd2dd;}
  .tree .f:hover{background:var(--panel2);}
  .tree .f.dir{color:var(--muted);}
  .editor-wrap{flex:1;display:flex;flex-direction:column;min-width:0;}
  .tabbar{height:34px;background:var(--panel);border-bottom:1px solid var(--line);display:flex;align-items:center;padding:0 12px;gap:10px;font-size:13px;color:var(--muted);}
  .tabbar .save{margin-left:auto;background:var(--accent);color:#1a0e06;border:none;border-radius:6px;padding:5px 12px;font-size:12px;font-weight:600;cursor:pointer;}
  textarea#editor{flex:1;width:100%;resize:none;border:none;outline:none;background:#0c0e12;color:#e6e8f0;font-family:var(--mono);font-size:13px;line-height:1.5;padding:14px;tab-size:2;}
  .chat{width:380px;background:var(--panel);border-left:2px solid var(--line);display:flex;flex-direction:column;}
  .chat-head{padding:10px 12px;font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:1px;border-bottom:1px solid var(--line);}
  .msgs{flex:1;overflow:auto;padding:12px;display:flex;flex-direction:column;gap:10px;}
  .msg{font-size:13px;line-height:1.55;border-radius:8px;padding:9px 11px;max-width:100%;white-space:pre-wrap;word-break:break-word;}
  .msg.user{background:var(--panel2);align-self:flex-end;}
  .msg.ai{background:#12161d;border:1px solid var(--line);}
  .msg.tool{background:none;border-left:3px solid var(--accent);padding:4px 10px;color:var(--muted);font-family:var(--mono);font-size:12px;}
  .composer{border-top:1px solid var(--line);padding:10px;display:flex;gap:8px;}
  .composer textarea{flex:1;resize:none;height:46px;background:var(--panel2);border:1px solid var(--line);border-radius:8px;color:var(--text);padding:9px;font-size:13px;font-family:inherit;outline:none;}
  .composer textarea:focus{border-color:var(--accent);}
  .composer button{background:var(--accent);color:#1a0e06;border:none;border-radius:8px;padding:0 16px;font-weight:600;cursor:pointer;}
  .modal{position:fixed;inset:0;background:rgba(0,0,0,.6);display:none;place-items:center;}
  .modal.open{display:grid;}
  .card{background:var(--panel);border:2px solid var(--line);border-radius:12px;padding:22px;width:360px;}
  .card h3{font-size:16px;margin-bottom:14px;}
  .card label{display:block;font-size:12px;color:var(--muted);margin:10px 0 4px;}
  .card input,.card select{width:100%;background:var(--panel2);border:1px solid var(--line);border-radius:7px;color:var(--text);padding:9px;font-size:13px;outline:none;}
  .card .row{display:flex;gap:10px;margin-top:18px;}
  .card .row button{flex:1;border:none;border-radius:8px;padding:10px;font-weight:600;cursor:pointer;}
  .card .save{background:var(--accent);color:#1a0e06;}
  .card .cancel{background:var(--panel2);color:var(--text);border:1px solid var(--line);}
  .empty{color:var(--muted);font-size:13px;padding:14px;text-align:center;}
</style>
</head>
<body>
  <div class="titlebar">
    <span class="logo">⚒</span><b>Anvil</b>
    <span class="sp"></span>
    <button onclick="openFolder()">Open Folder</button>
    <button onclick="openSettings()">⚙ Settings</button>
  </div>

  <div class="main">
    <div class="sidebar">
      <div class="side-head">Explorer</div>
      <div class="tree" id="tree"><div class="empty">Open a folder to start.</div></div>
    </div>

    <div class="editor-wrap">
      <div class="tabbar"><span id="openPath">No file open</span><button class="save" onclick="saveFile()">Save</button></div>
      <textarea id="editor" spellcheck="false" placeholder="Open a file from the explorer…"></textarea>
    </div>

    <div class="chat">
      <div class="chat-head">AI Agent</div>
      <div class="msgs" id="msgs"><div class="empty">Set your API key in Settings, then ask me to build or change something.</div></div>
      <div class="composer">
        <textarea id="prompt" placeholder="Ask Anvil… (Ctrl+Enter to send)"></textarea>
        <button onclick="send()">Send</button>
      </div>
    </div>
  </div>

  <div class="modal" id="settings">
    <div class="card">
      <h3>Settings — Bring Your Own Key</h3>
      <label>Provider</label>
      <select id="cfgProvider"><option value="openai">OpenAI-compatible</option><option value="anthropic">Anthropic (Claude)</option></select>
      <label>API Key</label>
      <input id="cfgKey" type="password" placeholder="sk-..." />
      <label>Model</label>
      <input id="cfgModel" placeholder="gpt-4o-mini" />
      <label>Base URL (optional)</label>
      <input id="cfgBase" placeholder="https://api.openai.com" />
      <div class="row"><button class="cancel" onclick="closeSettings()">Cancel</button><button class="save" onclick="saveSettings()">Save</button></div>
    </div>
  </div>

<script>
  // ---- native bridge (request/response over webview messaging) ----
  const pending = {};
  let reqId = 0;
  function native(type, payload){
    return new Promise((resolve)=>{
      const id = ++reqId;
      pending[id] = resolve;
      window.chrome.webview.postMessage(JSON.stringify({id, type, ...payload}));
    });
  }
  window.chrome.webview.addEventListener('message', (e)=>{
    let m; try{ m = JSON.parse(e.data); }catch{ return; }
    if(m.id && pending[m.id]){ pending[m.id](m); delete pending[m.id]; }
  });

  // ---- config ----
  let cfg = {};
  (async()=>{ const r = await native('loadConfig', {}); cfg = (r && r.cfg) || {}; })();
  function openSettings(){
    cfgProvider.value=cfg.provider||'openai'; cfgKey.value=cfg.apiKey||'';
    cfgModel.value=cfg.model||''; cfgBase.value=cfg.baseUrl||'';
    settings.classList.add('open');
  }
  function closeSettings(){ settings.classList.remove('open'); }
  function saveSettings(){
    cfg.provider=cfgProvider.value; cfg.apiKey=cfgKey.value;
    cfg.model=cfgModel.value || (cfg.provider==='anthropic'?'claude-3-5-sonnet-latest':'gpt-4o-mini');
    cfg.baseUrl=cfgBase.value || (cfg.provider==='anthropic'?'https://api.anthropic.com':'https://api.openai.com');
    native('saveConfig', {cfg});
    closeSettings();
  }

  // ---- file tree / editor ----
  let rootDir = '';
  let openFilePath = '';
  async function openFolder(){
    const r = await native('openFolder', {});
    if(!r.path) return;
    rootDir = r.path;
    await refreshTree();
  }
  async function refreshTree(){
    const r = await native('list', {path: rootDir});
    const tree = document.getElementById('tree');
    if(!r.entries || !r.entries.length){ tree.innerHTML='<div class="empty">(empty folder)</div>'; return; }
    tree.innerHTML = r.entries.map(en=>
      `<div class="f ${en.dir?'dir':''}" onclick="${en.dir?'':`openFile('${en.path.replace(/\\/g,'\\\\')}')`}">${en.dir?'▸ ':''}${en.name}</div>`
    ).join('');
  }
  async function openFile(path){
    const r = await native('read', {path});
    if(r.error){ alert(r.error); return; }
    openFilePath = path; document.getElementById('openPath').textContent = path;
    document.getElementById('editor').value = r.content||'';
  }
  async function saveFile(){
    if(!openFilePath) return;
    await native('write', {path: openFilePath, content: document.getElementById('editor').value});
    addMsg('tool', 'saved '+openFilePath);
  }

  // ---- chat / agent ----
  function addMsg(kind, text){
    const box = document.getElementById('msgs');
    if(box.querySelector('.empty')) box.innerHTML='';
    const d = document.createElement('div'); d.className='msg '+kind; d.textContent=text; box.appendChild(d);
    box.scrollTop = box.scrollHeight; return d;
  }

  const tools = {
    async read_file(a){ const r=await native('read',{path:a.path}); return r.error||r.content; },
    async write_file(a){ if(!confirm('Write to '+a.path+'?')) return 'declined'; await native('write',{path:a.path,content:a.content}); if(a.path===openFilePath) document.getElementById('editor').value=a.content; return 'written'; },
    async list_dir(a){ const r=await native('list',{path:a.path||rootDir}); return (r.entries||[]).map(e=>(e.dir?'[dir] ':'      ')+e.name).join('\n'); },
    async run_command(a){ if(!confirm('Run: '+a.command)) return 'declined'; const r=await native('run',{command:a.command,cwd:rootDir}); return r.output||'(no output)'; },
  };

  const SYS = "You are Anvil, a precise AI coding assistant inside a native desktop app. Use the tools to read/write files, list directories and run commands. Inspect before editing, make minimal correct changes, and give a short final summary.";
  const openaiToolDefs = [
    {type:'function',function:{name:'read_file',description:'Read a text file.',parameters:{type:'object',properties:{path:{type:'string'}},required:['path']}}},
    {type:'function',function:{name:'write_file',description:'Create/overwrite a file (asks user).',parameters:{type:'object',properties:{path:{type:'string'},content:{type:'string'}},required:['path','content']}}},
    {type:'function',function:{name:'list_dir',description:'List a directory.',parameters:{type:'object',properties:{path:{type:'string'}}}}},
    {type:'function',function:{name:'run_command',description:'Run a shell command (asks user).',parameters:{type:'object',properties:{command:{type:'string'}},required:['command']}}},
  ];

  let messages = [{role:'system',content:SYS}];
  async function send(){
    if(!cfg.apiKey){ openSettings(); return; }
    const ta = document.getElementById('prompt'); const text = ta.value.trim(); if(!text) return;
    ta.value=''; addMsg('user', text); messages.push({role:'user',content:text});
    if(cfg.provider==='anthropic') await loopAnthropic(); else await loopOpenAI();
  }
  document.getElementById('prompt').addEventListener('keydown',e=>{ if(e.ctrlKey&&e.key==='Enter') send(); });

  async function loopOpenAI(){
    for(let i=0;i<25;i++){
      const res = await fetch(cfg.baseUrl+'/v1/chat/completions',{method:'POST',headers:{'Content-Type':'application/json','Authorization':'Bearer '+cfg.apiKey},body:JSON.stringify({model:cfg.model,messages,tools:openaiToolDefs,tool_choice:'auto'})});
      if(!res.ok){ addMsg('ai','API error '+res.status+': '+(await res.text()).slice(0,300)); return; }
      const j = await res.json(); const m = j.choices[0].message; messages.push(m);
      if(m.tool_calls && m.tool_calls.length){
        for(const c of m.tool_calls){
          let args={}; try{args=JSON.parse(c.function.arguments);}catch{}
          addMsg('tool','→ '+c.function.name+' '+JSON.stringify(args));
          const result = await (tools[c.function.name]?.(args) ?? Promise.resolve('unknown tool'));
          messages.push({role:'tool',tool_call_id:c.id,content:String(result)});
        }
        continue;
      }
      if(m.content) addMsg('ai', m.content);
      return;
    }
  }

  async function loopAnthropic(){
    const aDefs = openaiToolDefs.map(t=>({name:t.function.name,description:t.function.description,input_schema:t.function.parameters}));
    const amsgs = messages.filter(m=>m.role!=='system').map(m=>({role:m.role==='tool'?'user':m.role,content:m.content}));
    for(let i=0;i<25;i++){
      const res = await fetch(cfg.baseUrl+'/v1/messages',{method:'POST',headers:{'Content-Type':'application/json','x-api-key':cfg.apiKey,'anthropic-version':'2023-06-01'},body:JSON.stringify({model:cfg.model,max_tokens:4096,system:SYS,messages:amsgs,tools:aDefs})});
      if(!res.ok){ addMsg('ai','API error '+res.status+': '+(await res.text()).slice(0,300)); return; }
      const j = await res.json(); amsgs.push({role:'assistant',content:j.content});
      const toolResults=[]; let finalText='';
      for(const b of j.content){
        if(b.type==='text') finalText+=b.text;
        else if(b.type==='tool_use'){ addMsg('tool','→ '+b.name+' '+JSON.stringify(b.input)); const r=await (tools[b.name]?.(b.input)??'unknown'); toolResults.push({type:'tool_result',tool_use_id:b.id,content:String(r)}); }
      }
      if(toolResults.length){ amsgs.push({role:'user',content:toolResults}); continue; }
      if(finalText) addMsg('ai', finalText);
      return;
    }
  }
</script>
</body>
</html>
)HTMLDOC";
