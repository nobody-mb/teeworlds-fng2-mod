#ifndef GAME_SERVER_RCD_H
#define GAME_SERVER_RCD_H

class RajhCheatDetector
{
public:
       static void ForgetAllClients();
       static void OnPlayerEnter(CPlayer * Player);
       static void OnPlayerLeave(CPlayer * Player);
       static void OnFire(CPlayer * Player);
       static void OnHit(CPlayer * Player, int Victim);
       static void OnTick(CPlayer * Player);

private:
       static void CheckWarnings(CPlayer * Player);
       static void AddWarning(CPlayer * Player, int amount = 1);
       static bool CheckFastChange(CPlayer * Player);
       static bool CheckInputPos(CPlayer * Player, int Victim);
       static bool CheckReflex(CPlayer * Player, int Victim);
       static bool CheckFastFire(CPlayer * Player);
};

#endif