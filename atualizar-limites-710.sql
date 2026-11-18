USE DragonRajaDB;
SET XACT_ABORT ON;

BEGIN TRANSACTION;

UPDATE dbo.ability_limit
SET Str = 710,
    Dex = 710,
    Con = 710,
    Wis = 710,
    [Int] = 710,
    Cha = 710,
    Endu = 710,
    Movp = 710,
    Mor = 710,
    Luck = 710,
    WS = 710,
    PS = 710
WHERE pre_Class BETWEEN 0 AND 4
  AND Step BETWEEN 0 AND 5;

IF @@ROWCOUNT <> 30
    THROW 50001, 'Esperadas 30 combinacoes de classe e etapa Dual.', 1;

COMMIT TRANSACTION;
