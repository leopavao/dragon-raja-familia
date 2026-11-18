#pragma once
void HandleLocalAdmin(int connection, const void* payload, int length);

void HandleLocalAdminVerification(const void* payload, int length);
void HandleLocalAdminMapNotice(const void* payload, int length);
void HandleLocalAdminMapRoster(const void* payload, int length);
