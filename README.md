<div align="center">

# Cursor Trail

<p>Overlay Windows native yang membawa cursor trail ke layar desktop.</p>

[![C++](https://img.shields.io/badge/C%2B%2B-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)](https://isocpp.org)
[![Windows](https://img.shields.io/badge/Windows-0078D6?style=for-the-badge&logo=windows&logoColor=white)](https://www.microsoft.com/windows)
[![Win32](https://img.shields.io/badge/Win32-0078D4?style=for-the-badge&logo=windows&logoColor=white)](https://learn.microsoft.com/windows/win32/)

</div>

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

Prasyarat build: [Visual Studio 2026](https://aka.ms/vs/stable/vs_community.exe) (Community/Professional/Enterprise) atau [Build Tools](https://aka.ms/vs/stable/vs_buildtools.exe) dengan workload **Desktop development with C++**, plus Windows SDK. Binary memakai static C++ runtime (`/MT`), sehingga pengguna yang menjalankan `CursorTrail.exe` tidak perlu menginstal Visual Studio maupun runtime tambahan apa pun.

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
