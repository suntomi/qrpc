import { build, context } from 'esbuild';

const buildOptions = {
  entryPoints: ['src/index.ts'],
  bundle: true,
  minify: true,
  format: 'iife',
  globalName: 'QRPC',
  outfile: 'dist/qrpc.bundle.js',
  sourcemap: true,
  target: 'es2020',
  define: {
    'process.env.NODE_ENV': '"production"'
  },
  // 外部依存関係があれば除外（ブラウザ用なのでnode_modulesは含める）
  // external: []
};

// コマンドライン引数でwatchモードかどうかを判定
const isWatch = process.argv.includes('--watch');

if (isWatch) {
  console.log('🔍 Starting bundle watch mode...');
  
  // Watch mode
  const ctx = await context(buildOptions);
  await ctx.watch();
  
  // Clean up context on process exit
  process.on('SIGINT', async () => {
    console.log('\n👋 Stopping watch mode...');
    await ctx.dispose();
    process.exit(0);
  });
  
  console.log('✅ Watch mode started. Monitoring TypeScript files for changes...');
} else {
  // Normal build mode
  console.log('🔨 Building bundle...');
  build(buildOptions).then(() => {
    console.log('✅ Bundle created successfully: dist/qrpc.bundle.js');
  }).catch(() => {
    console.error('❌ Bundle build failed');
    process.exit(1);
  });
}
