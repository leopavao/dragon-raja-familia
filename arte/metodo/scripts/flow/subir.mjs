import { chromium } from 'file:///C:/ferramentas/google-flow-mcp/node_modules/playwright-core/index.mjs';
const arquivos = process.argv.slice(2);
const PROJ = 'ce7ecfba-9ec3-4a9d-89e2-8e4a4a8fffd7';
const b = await chromium.connectOverCDP('http://127.0.0.1:9222');
const ctx = b.contexts()[0];
let p = ctx.pages().find((x) => x.url().includes(PROJ));
if (!p) { p = await ctx.newPage(); await p.goto(`https://flow.google.com/project/${PROJ}?hl=en`); await p.waitForTimeout(8000); }
for (const arquivo of arquivos) {
  for (let i = 0; i < 3; i++) { await p.keyboard.press("Escape"); await p.waitForTimeout(300); }
  const add = p.getByRole('button', { name: /Add ingredients/i }).first();
  if (await add.count()) await add.click(); else await p.mouse.click(512, 868);
  await p.waitForTimeout(1500);
  const bt = p.getByRole('button', { name: /Upload media/ }).first();
  const [fc] = await Promise.all([p.waitForEvent('filechooser', { timeout: 15000 }), bt.click()]);
  await fc.setFiles(arquivo); console.log('ENVIADO', arquivo);
  await p.waitForTimeout(12000);
  for (let i = 0; i < 3; i++) { await p.keyboard.press('Escape'); await p.waitForTimeout(400); }
}
await p.screenshot({ path: 'C:/dr-sprites/flow/_tela.png' });
await b.close();
