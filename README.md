# Cursor Trail

Overlay Windows native yang membawa cursor trail ke layar desktop Anda.

Aplikasi berjalan sebagai executable C++ Win32 x64 tanpa WebView, global hook, atau injection ke proses lain.

## Menjalankan

```powershell
.\run.ps1
```

Hasil build berada di `bin\CursorTrail.exe`. Klik kanan ikon pada tray untuk mengganti warna glow, memilih gaya trail, memilih mode performa, pause/resume, atau keluar. Klik ganda ikon tray untuk pause/resume cepat.

Untuk menjalankan binary yang sudah ada tanpa build ulang:

```powershell
.\run.ps1 -NoBuild
```

## Build

```powershell
.\build.ps1
```

Prasyarat build: Visual Studio Build Tools dengan workload **Desktop development with C++** dan Windows SDK. Binary memakai static C++ runtime (`/MT`), sehingga komputer tujuan tidak memerlukan instalasi runtime tambahan.

## Desain performa

- Gerakan mouse diterima secara event-driven melalui Win32 Raw Input (`RIDEV_INPUTSINK`); tidak ada global mouse hook.
- Gaya trail `Line` (default) memakai ring buffer titik maksimal 150 dan meraster segmen garis lurus ber-warna glow langsung ke buffer, sehingga tetap ringan tanpa alokasi bitmap per segmen. Gaya `Bat` memakai partikel sprite maksimal 24 (normalnya sekitar 18 karena lifetime 700 ms).
- Default `Eco` merender sekitar 24 FPS. Tersedia `Balanced` 30 FPS dan `Smooth` 60 FPS bila ingin animasi lebih halus.
- Timer animasi hanya hidup ketika ada partikel; saat idle renderer dan timer berhenti total.
- Bitmap layered window hanya sebesar bounding box partikel aktif, bukan seluruh virtual desktop.
- Sprite PNG transparan terkompresi hanya sekitar 5 KB dan ditanam langsung ke executable.
- Offset, drift, rotasi, skala, opacity, throttle 40 ms, dan lifetime 700 ms.
- Satu instance aplikasi dijaga dengan named mutex.

Untuk gaya trail `Line`, titik direkam sepanjang gerakan lalu ditarik sebagai segmen ber-glow + core, namun diimplementasikan ulang sebagai rasterizer C++ ringan tanpa GDI+ `Pen` per segmen.

## Batasan

- Overlay mungkin tidak terlihat pada game fullscreen exclusive.
- Sebaiknya tutup overlay ketika memakai game kompetitif dengan anti-cheat ketat.
- Opsi `Hide on non-arrow cursor` membandingkan handle cursor aktif dengan cursor panah sistem; custom cursor pack tertentu mungkin tidak dikenali sebagai panah.
