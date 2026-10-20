# Guardian Bluetooth web UI

`GuardianBluetooth.tsx` and `guardian-bluetooth.css` are the maintained source
overlay for SensESP 3.6.0. The production Vite bundle is stored in
`generated/frontend_files.h`; PlatformIO installs that header into the resolved
SensESP dependency before compiling the HALMET Guardian target. This keeps the
device UI reproducible without committing SensESP's complete frontend source
tree or requiring Node.js during every firmware build.

To regenerate the bundle, check out SensESP tag `v3.6.0`, copy the component
and stylesheet into its `frontend/src/components` folder, import the component
on both the Control and Configuration pages, then run `pnpm run css-compile`
and `pnpm run build`. Convert `dist/index.html` with SensESP's
`scripts/convert_html.py` and replace the generated header here.
