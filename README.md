This is another thermometer at DS18B20 + NodeMCU + Wiznet W5500, but at this time for Zabbix. 
Thanks for Arduino samples also this post https://habr.com/ru/post/405077/ for idea.

* Support multiple sensors (tested with 12 at same time). Use for first probe item `env.temp.0`.
To check probe availability, use `agent.ping` item.
* Added web-interface with CSS to make it look better.
* Added Setup page with password. Default login `admin` and password `admin`. You can change network setting also login and password from Setup page.
* Settings save into EEPROM.
* Default IP is `192.168.1.200`
* Added JSON. `<IP>/json`. Tested with OpenHab2.

_____
Ещё один термометр на NodeMCU + DS18B20 + Wiznet W5500, на этот раз для работы с Zabbix. Спасибо примерам Arduino и посту https://habr.com/ru/post/405077/ за идею. 

* Поддерживает несколько датчиков (проверено с 12 одновременно). Первый датчик будет `env.temp.0`. Для проверки доступности используйте `agent.ping`.
* Добавлен web-интерфейс, а в него немного CSS, чтобы смотрелось несколько симпатичнее.
* Добавлена страница с настройками. Она за паролем. стандартные логин `admin` и пароль `admin` Теперь можно менять сетевые параметры, логин и пароль из web-интерфейса. 
* Настройки сохраняются в EEPROM.
* IP по умолчанию `192.168.1.200`
* Добавлена выдача JSON. `<IP>/json`. Протестировано с OpenHab2
