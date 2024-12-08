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
<img src="assets/Schematic ESP" alt="schematic esp" width="500">
  
## 🎥**Demo Sistem**
Video demo sistem [Google Drive](https://drive.google.com/file/d/1GDtVoEBxEhWDBnP2TLctg90ebRj87ZnJ/view?usp=drive_link).

## 💡**Kesimpulan**
Sistem Monitoring Lonely Death adalah solusi inovatif yang dirancang untuk meningkatkan pengawasan kondisi lansia dengan memanfaatkan teknologi Edge AI. Sistem ini menggunakan ESP32-WROVER-CAM dan algoritma ResNet101 untuk mendeteksi postur tubuh secara real-time, seperti berdiri, duduk, atau berbaring, dan memberikan peringatan jika mendeteksi kondisi abnormal, seperti berbaring tanpa gerakan dalam waktu lama. Dengan memanfaatkan komunikasi TCP/IP antara ESP32 sebagai server dan Edge Gateway untuk pemrosesan data, sistem ini menghadirkan deteksi yang cepat, akurat, dan efisien. Solusi ini tidak hanya hemat biaya dengan perangkat terjangkau, tetapi juga mendukung privasi data melalui pengolahan lokal. Melalui pendekatan ini, sistem dapat meningkatkan kualitas hidup lansia sekaligus menjadi landasan bagi pengembangan teknologi pengawasan cerdas berbasis Edge AI di masa depan.


## 🙏🏻**Terima Kasih**
