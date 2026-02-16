// @title Sensor Read
// @description Read temperature and humidity values
var temp = sensor.getValue(0);
var humidity = sensor.getValue(1);

print("Temperature:", temp, "°C");
print("Humidity:", humidity, "%");
