# tarsau

Sistem Programlama 2025-2026 Bahar Donemi projesi icin C dilinde yazilmis,
sikistirma yapmadan metin dosyalarini `.sau` arsivine birlestiren ve arsivden
geri acan komut satiri programidir.

## Derleme

```sh
make
```

## Arsiv olusturma

```sh
./tarsau -b t1 t2 t3 t4.txt t5.dat -o s1.sau
```

`-o` verilmezse varsayilan cikti dosyasi `a.sau` olur.

## Arsiv acma

```sh
./tarsau -a s1.sau d1
```

Dizin parametresi verilmezse dosyalar gecerli dizine acilir.

## Test

```sh
make test
```

Test hedefi ornek dosyalari arsivler, yeni bir dizine acar ve `diff` ile
iceriklerin ayni kaldigini dogrular.

## Notlar

- En fazla 32 giris dosyasi kabul edilir.
- Giris dosyalarinin toplam boyutu 200 MB'i gecemez.
- Girisler ASCII metin dosyasi olmak zorundadir.
- Arsivden acilan dosyalara arsivlenmeden onceki izinler geri verilir.
