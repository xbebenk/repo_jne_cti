#ifndef __VersionH__
#define __VersionH__

#define VERSION        "2.2.1"
#define SPECIALVERSION "JNE"

#endif

//*******Release Notes******//
//2.2.0 (2013-03-05)
// Minor Update pindahin printf all agent saat dapet request dari ivr ke log biar nggak terlalu penuh layarnya
// digantikan dengan perhitungan agent_count yang di test untuk menjadi candidate
// 

//2.2.0 (2013-02-23)
// Update di appsvr agent, bagian session handler, kalo koneksi agent drop agent nya di logoutkan dari acd dan extension di release


//2.1.9 (2013-02-18)
//- update set ACD_PHONESTATUS_RESERVED di LIFC

//2.1.8 (2013-02-13)
//- Update logger agent status change

//2.1.7 (2013-02-08)
//- Update logger saat agent login dan logout

//2.1.6 (2013-01-17)
//- Update ivr_data di isi session_key dari call sebelumnya

//2.1.5 (2013-01-16)
//- Update Bug thread dblog sering berhenti setelah beberapa lama, ternyata karena messaging dari cti_tapi nilainya tidak selalu terisi
//- Update ivr_duration diisi saat call idle di device ivr
//- Update ivr_port di call_session
//- Update call_session di agent di hubungkan dengan call_session saat di ivr dengan d_number di isi extension ivr


//2.1.4 (2013-01-14)
//- Update insert data call_queue di table system_statistic
//- Update status di table call_session apabila call terjadi queue (2001) dan juga que disconnected (2002)
// TIDUR DULU NIH>>>> MASIH BANYAK YANG BLOM>> DAH PUYENG

//2.1.3
//- Update methode lineAnswer on tapi_OnCtiMsgAnswerCall bila mendapat error NOT_OWNER privilege di set menjadi OWNER baru kemudian di ulangi lagi

//2.1.2
//- Updated logtype, masing-masing module bisa ditambah option untuk di log atau tidak
// logmodule 1 = acd
// logmodule 2 = agent
// logmodule 3 = cti
// logmodule 4 = db
// logmodule 5 = manager

// penambahan configurasi di file smartcenter.conf
//	[log]
//	loglevel=5
//	acd=yes
//	agent=yes
//	cti=yes
//	db=yes
//	manager=yes

//
//2.1.1 (2013-01-12)
//- Updated loglevel untuk fungsi logger_Print


//*******Known Bugs******//
//bila agent id melebihi angka 255, scagentapplet selalu mengirim agent id 319 ke cti, 
//sehingga untuk saat ini maximum hanya 256 agent yang bisa di layani login ke ACD

//masih sering terjadi pesan "phone feature rejected" yang tampil di frame telephony
//mungkin perlu di log juga di sisi applet atau cticlient.php message apa yang di terima dari cti saat pesan tersebut muncul
