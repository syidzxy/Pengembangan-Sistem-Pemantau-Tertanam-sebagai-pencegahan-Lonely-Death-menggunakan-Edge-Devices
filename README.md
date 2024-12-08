### Sistem Monitoring Lonely Death

## 📌  **Deskripsi**
Sistem ini dirancang untuk mencegah lonely death pada lansia dengan memanfaatkan teknologi Edge AI. Menggunakan kamera ESP32-WROVER-CAM, sistem mendeteksi postur tubuh dan memproses data menggunakan algoritma ResNet101 yang memiliki akurasi tinggi. Jika terdeteksi kondisi abnormal, seperti berbaring tanpa gerakan selama lebih dari 15 detik, buzzer akan menyala sebagai peringatan. Komponen utama meliputi ESP32, modul kamera, LED, buzzer, dan Edge Gateway untuk pengolahan data. Sistem ini menawarkan solusi praktis dan efisien untuk pengawasan mandiri lansia, membantu meningkatkan kualitas hidup mereka.

<img src="assets/15-cara-merawat-lansia-dengan-benar-di-rumah.jpg" alt="lansia" width="500">

## 📝**Rumusan Masalah**
- Keterbatasan sistem pengawasan yang efisien
- Keterbatasan deteksi situasi kritis secara Real-Time
- Masalah pengelolaan data
- Kendala biaya yang tinggi
- Minimnya pendekatan secara menyeluruh
- Sulit untuk mengembangkan sistem

## 🤝**Solusi**
Dengan memanfaatkan 2 Processing Unit, Sistem dapat melakukan pemantauan kondisi postur dari pengguna.

ESP32 berperan sebagai sebuah Server dimana akan menyediakan data seperti tangkapan gambar kamera serta warning yang diberikan apabila sistem tidak mendeteksi pergerakkan pengguna ketika berbaring selama beberapa saat yang menunjukkan kondisi abnormal

## 🛠 **Spesifikasi sistem**
- Perangkat Edge: ESP32 with cam
- Buzzer
- Edge Gateway: PC Windows OS
- Protokol Komunikasi: TCP/IP
- Algoritma Pemrosesan: ResNet 101 + Convolutional Neural Network (CNN)
- Kelas yang Dideteksi: Berdiri, Duduk, Berbaring

## 📝**Flowchart Sistem**
<img src="assets/Flowchart sistem.jpg" alt="Flowchart" width="500">

## 📝**Alur Komunikasi**
<img src="assets/alur komunikasi.png" alt="Alur Komunikasi" width="500">

## 📝**Schematic ESP**
<img src="assets/Schematic ESP" alt="Alur Komunikasi" width="500">
  
## 🧪**Demo Sistem**
