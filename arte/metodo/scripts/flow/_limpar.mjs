import { chromium } from 'file:///C:/ferramentas/google-flow-mcp/node_modules/playwright-core/index.mjs';
const b = await chromium.connectOverCDP('http://127.0.0.1:9222');
const ps = b.contexts()[0].pages().filter((x) => x.url().includes('ce7ecfba'));
for (const p of ps.slice(1)) await p.close();
await ps[0].goto('https://flow.google.com/project/ce7ecfba-9ec3-4a9d-89e2-8e4a4a8fffd7?hl=en'); await ps[0].waitForTimeout(8000);
await ps[0].screenshot({ path: 'C:/dr-sprites/flow/_tela.png' }); console.log(b.contexts()[0].pages().length); await b.close();
