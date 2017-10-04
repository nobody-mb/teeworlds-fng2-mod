#include <engine/shared/config.h>

#include "player.h"
#include "gamecontext.h"
#include "entity.h"

#include "rcd.hpp"

#include <string>
#include <map>



  std::map<std::string, int> mapName;
  std::map<std::string, int> mapClan;
  std::map<std::string, int> mapIP;

static char aBuf[256];

void RajhCheatDetector::ForgetAllClients()
{
  mapName.clear();
  mapClan.clear();
  mapIP.clear();
}

// unfortuately called every tick by CCharacter::Tick()
// thus check real fire with TestFire()
void RajhCheatDetector::OnFire(CPlayer * Player)
{
       if(CheckFastChange(Player))
               AddWarning(Player);

       if(CheckFastFire(Player))
               AddWarning(Player, 3);
}


void RajhCheatDetector::OnHit(CPlayer * Player, int Victim)
{
       if(Player->GetCID() == Victim)
               return;

       if(CheckInputPos(Player, Victim))
               AddWarning(Player, 4);
       if(CheckReflex(Player, Victim))
               AddWarning(Player, 2);
}

void RajhCheatDetector::OnTick(CPlayer * Player)
{
       CheckWarnings(Player);
}

void RajhCheatDetector::OnPlayerEnter(CPlayer * Player)
{
  if(Player == 0)
    return;
  
      std::map<std::string, int>::iterator itName = mapName.find(std::string(Player->Server()->ClientName(Player->GetCID())));
      bool playerFound = itName != mapName.end();

      bool clanFound = mapClan.find(std::string(Player->Server()->ClientClan(Player->GetCID()))) != mapClan.end();
      
      Player->Server()->GetClientAddr(Player->GetCID(), aBuf, sizeof(aBuf));
      std::string ip = std::string(aBuf);
      std::map<std::string, int>::iterator itIP = mapIP.find(ip);
      bool ipFound = itIP != mapIP.end();
      
      if(playerFound && ipFound && clanFound)
      {
	// no doubt, this is the same guy
	Player->Warnings = itName->second;
        
        str_format(aBuf, sizeof(aBuf), "Welcome back: '%s' (name,ip,clan match)",Player->Server()->ClientName(Player->GetCID()));
        Player->GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "RCD", aBuf);
      }
      else if(playerFound && clanFound)
      {
	// very likely, he got a new ip
	Player->Warnings = itName->second;
        
        str_format(aBuf, sizeof(aBuf), "Welcome back: '%s' (name,clan match)",Player->Server()->ClientName(Player->GetCID()));
        Player->GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "RCD", aBuf);
      }
      else if(playerFound)
      {
        // well, maybe he got a new ip
        Player->Warnings = g_Config.m_RcdMaxWarnings - 2;
        
        str_format(aBuf, sizeof(aBuf), "Welcome back: '%s' (name match)",Player->Server()->ClientName(Player->GetCID()));
        Player->GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "RCD", aBuf);
      }
      else if(ipFound)
      {
	// time will show
 	Player->Warnings = g_Config.m_RcdMaxWarnings / 2;
        
        str_format(aBuf, sizeof(aBuf), "Welcome back: '%s' (ip match)",Player->Server()->ClientName(Player->GetCID()));
        Player->GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "RCD", aBuf);
      }
}

void RajhCheatDetector::OnPlayerLeave(CPlayer * Player)
{
       if(Player->Warnings >= g_Config.m_RcdMaxWarnings)
       {
	    std::string name = Player->Server()->ClientName(Player->GetCID());
	    std::string clan = Player->Server()->ClientClan(Player->GetCID());
	    
	    Player->Server()->GetClientAddr(Player->GetCID(), aBuf, sizeof(aBuf));
	    std::string ip = std::string(aBuf);
	    
	    mapName[name] = Player->Warnings;
	    mapClan[clan] = Player->Warnings;
	    mapIP[ip] = Player->Warnings;
       }
}

void RajhCheatDetector::AddWarning(CPlayer * Player, int amount)
{
       Player->Warnings += amount;
       Player->LastWarn = Player->Server()->Tick();
       
       str_format(aBuf, sizeof(aBuf), "'%s' warnings : %d",Player->Server()->ClientName(Player->GetCID()), Player->Warnings);
       Player->GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "RCD", aBuf);
}

void RajhCheatDetector::CheckWarnings(CPlayer * Player)
{
       if(Player->Warnings>0 && Player->Server()->Tick()-Player->LastWarn > Player->Server()->TickSpeed()*30)
       {
               Player->Warnings--;
               Player->LastWarn = Player->Server()->Tick();
	       
	       str_format(aBuf, sizeof(aBuf), "'%s' warnings : %d (30 sec without strange behavior)",Player->Server()->ClientName(Player->GetCID()), Player->Warnings);
               Player->GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "RCD", aBuf);
       }

       if(Player->Warnings >= g_Config.m_RcdMaxWarnings && g_Config.m_RcdEnable)
       {
	 // DONT KICK a PLAYER
	 // if one uses a bot and gets kicked, he'll probably come back
	 // if one uses a bot and gets banned, he may get a new IP and come back (like "TheEverest")
	 // so better just set down their health so they get killed by every single bullet, making them loose the fun of botting
	 
//                char buff[128];
//                str_format(buff, sizeof(buff), "'%s' has been kicked by RCD",Player->Server()->ClientName(Player->GetCID()));
//                Player->GameServer()->SendChat(-1,CGameContext::CHAT_ALL,buff);
                Player->Server()->Kick(Player->GetCID(), "kicked by experimental antibot");
	 if(CCharacter *c = Player->GetCharacter())
	 {
	       c->m_Health = 1;
	       c->m_Armor = 0;
	 }
       }
}

bool RajhCheatDetector::CheckFastChange(CPlayer * Player)
{
       CCharacter *CPlayer;
       if(!(CPlayer = Player->GameServer()->GetPlayerChar(Player->GetCID())))
               return false;

       if(CPlayer->m_LatestInput.m_TargetX == CPlayer->OldInput.m_TargetX && CPlayer->m_LatestInput.m_TargetY == CPlayer->OldInput.m_TargetY
               && CPlayer->m_LatestInput.m_TargetX != CPlayer->m_LatestPrevInput.m_TargetX && CPlayer->m_LatestInput.m_TargetY != CPlayer->m_LatestPrevInput.m_TargetY)
       {
	       str_format(aBuf, sizeof(aBuf), "'%s' aimed exactly at where he aimed 2 ticks ago",Player->Server()->ClientName(Player->GetCID()));
// 	       Player->GameServer()->SendChat(-1,CGameContext::CHAT_ALL,aBuf);
               Player->GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "RCD", aBuf);
               return true;
       }

       return false;
}

bool RajhCheatDetector::CheckInputPos(CPlayer * Player, int Victim)
{
       CCharacter *CPlayer;
       CCharacter *CVictim;
       if(!(CPlayer = Player->GameServer()->GetPlayerChar(Player->GetCID())) || !(CVictim = Player->GameServer()->GetPlayerChar(Victim)))
               return false;

       vec2 Target = vec2(CPlayer->m_LatestInput.m_TargetX, CPlayer->m_LatestInput.m_TargetY);
       vec2 TargetPos = CPlayer->m_Pos + Target;
       // Ping may fake this
       if(distance(TargetPos,CVictim->m_Pos) < 8.f)
       {
	       str_format(aBuf, sizeof(aBuf), "'%s' aimed exactly at '%s' position",Player->Server()->ClientName(Player->GetCID()), Player->Server()->ClientName(Victim));
// 	       Player->GameServer()->SendChat(-1,CGameContext::CHAT_ALL,aBuf);
               Player->GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "RCD", aBuf);
               return true;
       }

       return false;

}

bool RajhCheatDetector::CheckReflex(CPlayer * Player, int Victim)
{
       CCharacter *CPlayer;
       CCharacter *CVictim;
       if(!(CPlayer = Player->GameServer()->GetPlayerChar(Player->GetCID())) || !(CVictim = Player->GameServer()->GetPlayerChar(Victim)))
               return false;

       if(distance(CPlayer->m_Pos, CVictim->m_Pos) < Player->GameServer()->m_World.m_Core.m_Tuning.m_LaserReach+5 &&
	  distance(CPlayer->m_Pos, CVictim->m_Pos) > Player->GameServer()->m_World.m_Core.m_Tuning.m_LaserReach-5)
       {
	       str_format(aBuf, sizeof(aBuf), "'%s' aimed exactly at '%s' at max range",Player->Server()->ClientName(Player->GetCID()), Player->Server()->ClientName(Victim));
// 	       Player->GameServer()->SendChat(-1,CGameContext::CHAT_ALL,aBuf);
               Player->GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "RCD", aBuf);
               return true;
       }
       return false;
}

// adapted from CCharacter::CountInput()
static int TestFire(int prev, int cur)
{
       prev &= INPUT_STATE_MASK;
       cur &= INPUT_STATE_MASK;
       int i = prev;
       while(i != cur)
       {
               i = (i+1)&INPUT_STATE_MASK;
               if(i&1)
                       return 1;
       }

       return 0;
}
#include <iostream>
bool RajhCheatDetector::CheckFastFire(CPlayer * Player)
{
       CCharacter *c = Player->GameServer()->GetPlayerChar(Player->GetCID());
       if(!c || !Player || !TestFire(c->m_LatestPrevInput.m_Fire, c->m_LatestInput.m_Fire))
	 return false;
       
       
       bool result;
       
       if(Player->LastFireIdx >= Player->LastFireTick.size())
       {
         // we ve collected enough samples
         
         Player->LastFireIdx = 0;

std::cout << "lastfiretick array for player id " << Player->GetCID() << std::endl;
for (unsigned int i=0; i<Player->LastFireTick.size(); i++) 
{
std::cout << Player->LastFireTick[i] << ' ';
}
std::cout << std::endl;
         
         // derive to get the time diff between each fireing
         for(unsigned int i=0; i<Player->LastFireTick.size()-1; i++)
         {
           Player->LastFireTick[i] = Player->LastFireTick[i+1] - Player->LastFireTick[i];
         }
         unsigned int last = Player->LastFireTick.size()-1;
         Player->LastFireTick[last] = Player->Server()->Tick() - Player->LastFireTick[last];
         
for (unsigned int i=0; i<Player->LastFireTick.size(); i++) 
{
std::cout << Player->LastFireTick[i] << ' ';
}
std::cout << std::endl;
         
         // derive again to get the change of the diffs
         for(unsigned int i=0; i<Player->LastFireTick.size()-1; i++)
         {
           Player->LastFireTick[i] = Player->LastFireTick[i+1] - Player->LastFireTick[i];
         }
         Player->LastFireTick[last] = 0;
         
for (unsigned int i=0; i<Player->LastFireTick.size(); i++) 
{
std::cout << Player->LastFireTick[i] << ' ';
}
std::cout << std::endl;
         

//          Player->LastFireTick = std::abs(Player->LastFireTick);
         
         if(std::abs(Player->LastFireTick.sum()) <= 1)
         {
               str_format(aBuf, sizeof(aBuf), "'%s' fires way too regularly",Player->Server()->ClientName(Player->GetCID()));
               Player->GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "RCD", aBuf);
               
           result = true;
         }
         else
         {
           result = false;
         }
       }
       else
       {
         Player->LastFireTick[Player->LastFireIdx++] = Player->Server()->Tick();
         
         result = false;
       }
       
//        if(diff <= Player->Server()->TickSpeed()*0.002)
//        {
//                str_format(aBuf, sizeof(aBuf), "'%s' already fired %d ms ago",Player->Server()->ClientName(Player->GetCID()), diff);
//                Player->GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "RCD", aBuf);
//                
//                result = true;
//        }
//        else
//        {
//          result = false;
//        }
//        
//        Player->LastFireTick = Player->Server()->Tick();
       
       return result;
}
