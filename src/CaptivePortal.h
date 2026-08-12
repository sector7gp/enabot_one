#pragma once

void captivePortalBegin();
void captivePortalLoop();

// Baja el servidor web, el DNS, mDNS, OTA y apaga la radio WiFi. Despues de
// esto el portal no vuelve solo: hay que reiniciar con el boton apretado.
void captivePortalEnd();

// true mientras se esta recibiendo una actualizacion OTA. El timeout del
// portal tiene que respetarlo: cortar el WiFi a mitad de un flasheo deja la
// placa con una imagen incompleta.
bool captivePortalOtaInProgress();
