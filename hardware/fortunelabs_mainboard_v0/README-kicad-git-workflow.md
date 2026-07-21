# KiCad + Git Workflow

Template minimal untuk mengelola proyek KiCad (v6+) dengan git.

## Struktur folder

```
project-root/
├── .gitignore
├── .github/workflows/kicad-ci.yml
├── project.kicad_pro
├── project.kicad_sch
├── project.kicad_pcb
├── libraries/
│   ├── symbols/
│   ├── footprints.pretty/
│   └── 3dmodels/
└── docs/
    └── (datasheet, catatan desain, dsb.)
```

## Setup awal

```bash
git init
cp .gitignore /path/ke/project/
mkdir -p .github/workflows
cp kicad-ci.yml .github/workflows/
git add .
git commit -m "Initial commit: project skeleton"
```

Buka KiCad, di Project Manager aktifkan menu **Tools > Git** untuk commit/push/pull langsung dari GUI (tersedia sejak KiCad 7). Ini membantu karena KiCad tahu file mana yang relevan (skip cache/backup otomatis).

## Kebiasaan commit

Commit per perubahan logis: "tambah regulator 3.3V", "routing power plane", "ganti footprint konektor USB-C". Hindari satu commit besar untuk banyak perubahan — diff schematic/PCB (walau berbasis teks) tetap padat koordinat, jadi commit kecil memudahkan review dan revert.

## Review perubahan (diff visual)

Diff teks mentah `.kicad_sch`/`.kicad_pcb` sulit dibaca manusia. Gunakan salah satu:

- `kicad-cli sch export pdf` / `kicad-cli pcb export pdf` pada dua commit, lalu bandingkan PDF-nya.
- Plugin pihak ketiga seperti `kicad-diff` (render PNG per commit, diff visual side-by-side).
- KiCad 8's built-in "Compare" saat melihat riwayat git di GUI.

## Library

Kalau punya symbol/footprint kustom, taruh di `libraries/` dalam repo yang sama (untuk proyek kecil), atau jadikan repo terpisah + git submodule (untuk dipakai lintas proyek):

```bash
git submodule add https://github.com/username/kicad-libs.git libraries/shared
```

Daftarkan library itu di project-specific library table (Preferences > Manage Symbol/Footprint Libraries > pilih "Project" bukan "Global") supaya path relatif dan reproducible di komputer lain.

## Branching

- `main` — selalu dalam kondisi ERC/DRC bersih, siap fabrikasi.
- `feature/nama-fitur` — perubahan sedang berjalan (misal tambah sensor, ganti regulator).
- `rev-b`, `rev-c`, dst — kalau mau eksplisit menandai revisi board fisik.

Merge ke `main` setelah ERC/DRC lolos. Kalau branch schematic sudah divergen jauh, conflict di file S-expression susah di-resolve manual — kadang lebih cepat pilih satu versi lalu re-apply perubahan penting secara manual daripada merge otomatis.

## Tagging rilis

Tag setiap revisi yang benar-benar dikirim ke fab:

```bash
git tag -a v1.0-rev-a -m "Rev A dikirim ke JLCPCB 2026-07-21"
git push origin v1.0-rev-a
```

Ini membuat gerber yang diterima fab bisa ditelusuri balik ke commit exact.

## CI (opsional, sudah disiapkan di `.github/workflows/kicad-ci.yml`)

Setiap push/PR ke `main`, workflow otomatis:
1. Jalankan `kicad-cli sch erc` dan `kicad-cli pcb drc`.
2. Export gerber, drill file, dan BOM sebagai artifact.

Edit `PROJECT_NAME` di file workflow sesuai nama file proyekmu.
