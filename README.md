# The Ultimate ESP32 Hacking Tool: Because sometimes you just need to be a little evil!
### Looking to take down your neighbor's wifi network? Or just want to cause chaos at your local coffee shop? Or does your Boss pay you less than you deserve?
Look no further, because we have the perfect tool for you: the ESP32 hacking tool!

## Features
- **WiFi Beacon Spammer**: With our beacon spammer feature, you can flood the area with fake wifi signals and confuse the hell out of anyone trying to connect to a legitimate network.
- **WiFi Deauther**: And when things get a little too boring, use our deauther function to disconnect someone from the internet in a flash. It's like a digital version of pulling the plug on their router. And no it is not a WiFi jammer, it is a deauther. Don't know the difference? just google script kiddie.
- **Captive Portal**: Our captive portal feature allows you to redirect all incoming connections to a custom landing page, where you can collect login information. You can choose between a Google Login page and a McDonald's Free WiFi page. Why McDonald's? Because from now on you can earn McDonalds reward points as a fat but brave hacker for other people!
- **Evil Twin**: Our Evil Twin attack creates a replica of another WiFi network which gets permanently deauthenticated as well, so you can't connect to the original network. If you now connect to the fake wifi, a fake router login page opens, telling you that the router password has expired. if you finally received the complex password of your 80-year-old neighbor, you can inform him that "0123456789" is not the safest password.
- **WiFi Scanner**: Scan for nearby WiFi networks and their bssid, channels and signal strength.
  
If i have time for it, I will add a demonstration video.

![image](https://i.imgur.com/aPWmspx.jpeg)


## How to use 
If you are new to ESP32, please just google it, it is not that hard and the same procedure for every ESP32 board!

1. download the esp idf toolchain and the esp32-hacking-tool
2. connect your esp32 to your computer
3. open a terminal and navigate to the esp32-hacking-tool folder
4. run idf.py build flash

or just use the precompiled bin file 
1. connect your esp32 to your computer
2. To make sure to "clean" your esp32 just run **Mac/Linux**: esptool.py -p /dev/cu.<PORT> erase_flash **Windows**: esptool.py -p COM<PORT> erase_flash
2. open a terminal and navigate to the esp32_hackingtool/esptool_flash folder and run following command:  
**Mac/Linux:** esptool.py -p /dev/cu.<PORT> -b 1200000 --before=default_reset --after=hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x8000 partition-table.bin 0x1000 bootloader.bin 0x20000 hackingtool.bin
    
  **Windows**: esptool.py -p COM<PORT> -b 1200000 --before=default_reset --after=hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x8000 partition-table.bin 0x1000 bootloader.bin 0x20000 hackingtool.bin
3. flash the bin file with esptool.py

## Hardware Requirements
- ESP32 with TTGO T-Display (i used the 16MB version): [AliExpress](https://aliexpress.com/item/33050639690.html?algo_pvid=f3353b8c-edf0-4bca-8686-7e315f706d40&algo_exp_id=f3353b8c-edf0-4bca-8686-7e315f706d40-0&pdp_ext_f=%7B%22sku_id%22%3A%2212000022706983282%22%7D&pdp_npi=2%40dis%21EUR%2115.33%2114.41%21%21%21%21%21%402101e9d116721750836725640e9b03%2112000022706983282%21sea) or if you are rich and impatient [Amazon](https://www.amazon.de/Wireless-Bluetooth-T-Display-Entwicklungsplatine-Arduino/dp/B09WHS11BK/ref=sr_1_3?crid=1KVYX4CZDSRJS&keywords=ttgo+esp32&qid=1672175654&sprefix=ttgo+%2Caps%2C218&sr=8-3)
- USB-C cable
  
Apparently, you can use probably every ESP32 board with a OLED display, simply change the pin definitions in the menuconfig. Or you just code a website or uart terminal interface instead of using a external display.

## Extra Information
The code is fully written in the ESP-IDF framework, with a little API, so you may easily add new features.

Since I am not allowed to code stuff like this at my job, I decided to create this project in my free time (If you want to support me you can buy me a [coffee](https://www.buymeacoffee.com/kl0ibi)). I hope you enjoy it as much as I did creating it. If you have any questions or ideas, feel free to create issues or even better pull requests. I will try to answer them as soon as possible.
If I have more time, I will add some more features to this project, here are some ideas:

### Future plans
- Add a nfc module to read and write nfc tags
- Implement a wifi sniffer
- Implement some bluetooth tools


### Disclaimer: This project is for educational purposes only. I am not responsible for any damage you cause with this tool. Use it at your own risk. No animals were harmed during the development of this project.
