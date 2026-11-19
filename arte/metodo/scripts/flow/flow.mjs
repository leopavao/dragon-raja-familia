// Chama uma ferramenta do google-flow-mcp sem precisar do conector carregado no Claude.
// uso: node flow.mjs <ferramenta> '<json dos argumentos>'
import { Client } from 'file:///C:/ferramentas/google-flow-mcp/node_modules/@modelcontextprotocol/sdk/dist/esm/client/index.js';
import { StdioClientTransport } from 'file:///C:/ferramentas/google-flow-mcp/node_modules/@modelcontextprotocol/sdk/dist/esm/client/stdio.js';
const [ferramenta, args = '{}'] = process.argv.slice(2);
const t = new StdioClientTransport({
  command: 'node', args: ['C:/ferramentas/google-flow-mcp/dist/index.js'],
  env: { ...process.env, FLOW_PROFILE_DIR: 'C:/ferramentas/perfil-flow', FLOW_OUTPUT_DIR: 'C:/dr-sprites/_flow_saida', FLOW_MAX_CREDITS_PER_CALL: '20', FLOW_CDP_URL: 'http://127.0.0.1:9222' },
  stderr: 'inherit',
});
const c = new Client({ name: 'flow-cliente', version: '1' });
await c.connect(t);
try {
  if (!ferramenta) { const l = await c.listTools(); console.log(l.tools.map((x) => x.name).join('\n')); }
  else { const r = await c.callTool({ name: ferramenta, arguments: JSON.parse(args) }, undefined, { timeout: 600000 }); for (const x of r.content) console.log(x.text ?? JSON.stringify(x)); }
} finally { await c.close(); }
