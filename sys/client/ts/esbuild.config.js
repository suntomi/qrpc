import { build } from 'esbuild';

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

// バンドルビルド実行
build(buildOptions).catch(() => process.exit(1));
