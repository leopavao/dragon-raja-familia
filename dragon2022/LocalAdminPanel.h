#pragma once
void ToggleLocalAdminPanel();
void ReceiveLocalAdminReply(const void* payload, int length);
void ReceiveLocalAdminCatalog(const void* payload, int length);
struct tagMSG;
bool HandleLocalAdminMessage(tagMSG* message);
void UpdateLocalAdminLifeButton();
