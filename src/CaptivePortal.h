#pragma once

void captivePortalBegin();
void captivePortalLoop();

// Baja el servidor web, el DNS y apaga la radio WiFi. Despues de esto el
// portal no vuelve solo: hay que reiniciar con el boton apretado.
void captivePortalEnd();
