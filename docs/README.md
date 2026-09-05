# OYOBYOK docs

The documentation site, built with Docusaurus.

```bash
npm install
npm start
```

`npm run build` writes a static site to `build/`, and `npm run serve` serves it. The version pill in the navbar reads `OYOBYOK_VERSION` from `../firmware/shared/oyobyok_app.inc`, so the number follows the firmware without being written down twice.

`.github/workflows/docs.yml` builds and publishes the site to GitHub Pages on every push to `main`.
