/* (c) KeksTW    */
#include "fng2.h"
#include "../entities/character.h"
#include "../player.h"
#include "../fng2define.h"
#include <engine/shared/config.h>
#include <string.h>
#include <stdio.h>

#define ID_ENTRY(i) (GameServer()->m_pController->t_stats->find_round_id(i))


CGameControllerFNG2::CGameControllerFNG2(class CGameContext *pGameServer)
: IGameController((class CGameContext*)pGameServer)
{
	m_pGameType = "fng2";
	m_GameFlags = GAMEFLAG_TEAMS;
	
	if(m_Config.m_SvTournamentMode) m_Warmup = 60*Server()->TickSpeed();
	else m_Warmup = m_Config.m_SvWarmup;
}

CGameControllerFNG2::CGameControllerFNG2(class CGameContext *pGameServer, CConfiguration& pConfig)
: IGameController((class CGameContext*)pGameServer, pConfig)
{
	m_pGameType = "fng2";
	m_GameFlags = GAMEFLAG_TEAMS;
	
	if(m_Config.m_SvTournamentMode) m_Warmup = 60*Server()->TickSpeed();
	else m_Warmup = m_Config.m_SvWarmup;
}

void CGameControllerFNG2::Tick()
{
	// do warmup
	if(!GameServer()->m_World.m_Paused && m_Warmup)
	{
		if(m_Config.m_SvTournamentMode){
		} else {
			m_Warmup--;
			if(!m_Warmup)
				StartRound();
		}
	}

	if(m_GameOverTick != -1)
	{
		// game over.. wait for restart
		if(Server()->Tick() > m_GameOverTick+Server()->TickSpeed()*10)
		{
			if(m_Config.m_SvTournamentMode){
			} else {
				CycleMap();
				StartRound();
				m_RoundCount++;
			}
		}
	}
	else if(GameServer()->m_World.m_Paused && m_UnpauseTimer)
	{
		--m_UnpauseTimer;
		if(!m_UnpauseTimer)
			GameServer()->m_World.m_Paused = false;
	}

	
	// game is Paused
	if(GameServer()->m_World.m_Paused) {		
		if (m_GameOverTick == -1) {
		}
		if(GameServer()->m_World.m_Paused) ++m_RoundStartTick;
	}

	// do team-balancing
	if(IsTeamplay() && m_UnbalancedTick != -1 && Server()->Tick() > m_UnbalancedTick+m_Config.m_SvTeambalanceTime*Server()->TickSpeed()*60 && !m_Config.m_SvTournamentMode)
	{
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", "Balancing teams");

		int aT[2] = {0,0};
		float aTScore[2] = {0,0};
		float aPScore[MAX_CLIENTS] = {0.0f};
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			{
				aT[GameServer()->m_apPlayers[i]->GetTeam()]++;
				aPScore[i] = GameServer()->m_apPlayers[i]->m_Score*Server()->TickSpeed()*60.0f/
					(Server()->Tick()-GameServer()->m_apPlayers[i]->m_ScoreStartTick);
				aTScore[GameServer()->m_apPlayers[i]->GetTeam()] += aPScore[i];
			}
		}

		// are teams unbalanced?
		if(absolute(aT[0]-aT[1]) >= 2)
		{
			int M = (aT[0] > aT[1]) ? 0 : 1;
			int NumBalance = absolute(aT[0]-aT[1]) / 2;

			do
			{
				CPlayer *pP = 0;
				float PD = aTScore[M];
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!GameServer()->m_apPlayers[i] || !CanBeMovedOnBalance(i))
						continue;
					// remember the player who would cause lowest score-difference
					if(GameServer()->m_apPlayers[i]->GetTeam() == M && (!pP || absolute((aTScore[M^1]+aPScore[i]) - (aTScore[M]-aPScore[i])) < PD))
					{
						pP = (CPlayer*)GameServer()->m_apPlayers[i];
						PD = absolute((aTScore[M^1]+aPScore[i]) - (aTScore[M]-aPScore[i]));
					}
				}

				// move the player to the other team
				int Temp = pP->m_LastActionTick;
				pP->SetTeam(M^1);
				pP->m_LastActionTick = Temp;

				pP->Respawn();
				pP->m_ForceBalanced = true;
			} while (--NumBalance);

			m_ForceBalanced = true;
		}
		m_UnbalancedTick = -1;
	}

	// check for inactive players
	if(m_Config.m_SvInactiveKickTime > 0 && !m_Config.m_SvTournamentMode)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS && !Server()->IsAuthed(i))
			{
				if(Server()->Tick() > GameServer()->m_apPlayers[i]->m_LastActionTick+m_Config.m_SvInactiveKickTime*Server()->TickSpeed()*60)
				{
					switch(m_Config.m_SvInactiveKick)
					{
					case 0:
						{
							// move player to spectator
							((CPlayer*)GameServer()->m_apPlayers[i])->SetTeam(TEAM_SPECTATORS);
						}
						break;
					case 1:
						{
							// move player to spectator if the reserved slots aren't filled yet, kick him otherwise
							int Spectators = 0;
							for(int j = 0; j < MAX_CLIENTS; ++j)
								if(GameServer()->m_apPlayers[j] && GameServer()->m_apPlayers[j]->GetTeam() == TEAM_SPECTATORS)
									++Spectators;
							if(Spectators >= m_Config.m_SvSpectatorSlots)
								Server()->Kick(i, "Kicked for inactivity");
							else
								((CPlayer*)GameServer()->m_apPlayers[i])->SetTeam(TEAM_SPECTATORS);
						}
						break;
					case 2:
						{
							// kick the player
							Server()->Kick(i, "Kicked for inactivity");
						}
					}
				}
			}
		}
	}

	DoWincheck();
}

void CGameControllerFNG2::DoWincheck()
{
	if(m_GameOverTick == -1 && !m_Warmup && !GameServer()->m_World.m_ResetRequested)
	{
		if(IsTeamplay())
		{
			// check score win condition
			if((m_Config.m_SvScorelimit > 0 && (m_aTeamscore[TEAM_RED] >= m_Config.m_SvScorelimit || m_aTeamscore[TEAM_BLUE] >= m_Config.m_SvScorelimit)) ||
				(m_Config.m_SvTimelimit > 0 && (Server()->Tick()-m_RoundStartTick) >= m_Config.m_SvTimelimit*Server()->TickSpeed()*60))
			{
				if(m_Config.m_SvTournamentMode){
				} else {
					if(m_aTeamscore[TEAM_RED] != m_aTeamscore[TEAM_BLUE])
						EndRound();
					else
						m_SuddenDeath = 1;
				}
			}			
		}
		else
		{
			// gather some stats
			int Topscore = 0;
			int TopscoreCount = 0;
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(GameServer()->m_apPlayers[i])
				{
					if(GameServer()->m_apPlayers[i]->m_Score > Topscore)
					{
						Topscore = GameServer()->m_apPlayers[i]->m_Score;
						TopscoreCount = 1;
					}
					else if(GameServer()->m_apPlayers[i]->m_Score == Topscore)
						TopscoreCount++;
				}
			}

			// check score win condition
			if((m_Config.m_SvScorelimit > 0 && Topscore >= m_Config.m_SvScorelimit) ||
				(m_Config.m_SvTimelimit > 0 && (Server()->Tick()-m_RoundStartTick) >= m_Config.m_SvTimelimit*Server()->TickSpeed()*60))
			{
				if(TopscoreCount == 1)
					EndRound();
				else
					m_SuddenDeath = 1;
			}
		}
	}
}

void CGameControllerFNG2::Snap(int SnappingClient)
{
	IGameController::Snap(SnappingClient);
	
	CNetObj_GameData *pGameDataObj = (CNetObj_GameData *)Server()->SnapNewItem(NETOBJTYPE_GAMEDATA, 0, sizeof(CNetObj_GameData));
	if(!pGameDataObj)
		return;
	
	/*pGameDataObj->m_FlagCarrierRed = FLAG_ATSTAND;
	pGameDataObj->m_FlagCarrierBlue = FLAG_ATSTAND;*/

	pGameDataObj->m_TeamscoreRed = m_aTeamscore[TEAM_RED];
	pGameDataObj->m_TeamscoreBlue = m_aTeamscore[TEAM_BLUE];
}

void CGameControllerFNG2::OnCharacterSpawn(class CCharacter *pChr)
{
	// default health
	pChr->IncreaseHealth(10);

	// give default weapons
	pChr->GiveWeapon(WEAPON_HAMMER, -1);
	pChr->GiveWeapon(WEAPON_RIFLE, -1);
}
	
int CGameControllerFNG2::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	CPlayer *pPlVictim = NULL;
	struct tee_stats *s_killer = NULL, *s_victim = NULL;

	if (pVictim)
		pPlVictim = pVictim->GetPlayer();
	else	
		printf("couldnt find victim character\n");
		
	if (pKiller)
		s_killer = ID_ENTRY(pKiller->GetCID());
	else
		printf("couldnt find killer player entry\n");
	
	if (pPlVictim)
		s_victim = ID_ENTRY(pPlVictim->GetCID());
	else
		printf("couldnt find victim player entry\n");

	if (Weapon == WEAPON_GAME) {
		if (s_victim)
			s_victim->frozeby = -1;
		else
			printf("couldnt find victim\n");
		return 0;
	}

	if(pKiller == pVictim->GetPlayer()) {
		pVictim->GetPlayer()->m_selfkills++; // suicide
		if (s_victim)
			s_victim->frozeby = -1;
		else
			printf("couldnt find victim\n");
		if (s_killer)
			s_killer->suicides++;
		else
			printf("couldnt find killer\n");
	} else {
		if (Weapon == WEAPON_RIFLE || Weapon == WEAPON_GRENADE) {
			if (s_victim) {
				s_victim->frozen++;
				s_victim->frozeby = s_killer->id;
			} else {
				printf("couldnt find victim\n");
			}
			//printf("victim %d %s froze by %d %s\n", s_victim->id, ID_NAME(s_victim->id), 
			//	s_killer->id, ID_NAME(s_killer->id));
			if (s_killer) {
				s_killer->freezes++;
			} else {
				printf("couldnt find killer\n");
			}
			if(IsTeamplay() && pVictim->GetPlayer()->GetTeam() == pKiller->GetTeam())
				pKiller->m_teamkills++; // teamkill
			else {
				pKiller->m_kills++; // normal kill
				pVictim->GetPlayer()->m_hits++; //hits by oponent
				m_aTeamscore[pKiller->GetTeam()]++; //make this config.?
			}
		} else if(Weapon == WEAPON_SPIKE_NORMAL){
			if (s_killer) {
				s_killer->kills++;
			} else {
				printf("couldnt find killer\n");
			}
			if(pKiller->GetCharacter()) 
				GameServer()->MakeLaserTextPoints(pKiller->GetCharacter()->m_Pos, 
					pKiller->GetCID(), m_Config.m_SvPlayerScoreSpikeNormal);
			pKiller->m_grabs_normal++;
			pVictim->GetPlayer()->m_deaths++;		
			m_aTeamscore[pKiller->GetTeam()] += m_Config.m_SvTeamScoreSpikeNormal;
			pVictim->GetPlayer()->m_RespawnTick = 
				Server()->Tick()+Server()->TickSpeed()*.5f;
		} else if(Weapon == WEAPON_SPIKE_RED){
			if(pKiller->GetTeam() == TEAM_RED) {
				if (s_killer) {
					s_killer->kills_x2++;
				} else {
					printf("couldnt find killer\n");
				}
				pKiller->m_grabs_team++;
				pVictim->GetPlayer()->m_deaths++;
				m_aTeamscore[TEAM_RED] += m_Config.m_SvTeamScoreSpikeTeam;
				if(pKiller->GetCharacter())
					GameServer()->MakeLaserTextPoints(
						pKiller->GetCharacter()->m_Pos, pKiller->GetCID(),
						m_Config.m_SvPlayerScoreSpikeTeam);
			} else {
				if (s_killer) {
					s_killer->kills_wrong++;
				} else {
					printf("couldnt find killer\n");
				}
				pKiller->m_grabs_false++;				
				m_aTeamscore[TEAM_BLUE] += m_Config.m_SvTeamScoreSpikeFalse;
				if(pKiller->GetCharacter())
					GameServer()->MakeLaserTextPoints(
						pKiller->GetCharacter()->m_Pos, pKiller->GetCID(),
						m_Config.m_SvPlayerScoreSpikeFalse);
			}
			pVictim->GetPlayer()->m_RespawnTick = 
				Server()->Tick()+Server()->TickSpeed()*.5f;
		} else if(Weapon == WEAPON_SPIKE_BLUE){
			if(pKiller->GetTeam() == TEAM_BLUE) {
				if (s_killer) {
					s_killer->kills_x2++;
				} else {
					printf("couldnt find killer\n");
				}
				pKiller->m_grabs_team++;
				pVictim->GetPlayer()->m_deaths++;
				m_aTeamscore[TEAM_BLUE] += m_Config.m_SvTeamScoreSpikeTeam;
				if(pKiller->GetCharacter())
					GameServer()->MakeLaserTextPoints(
						pKiller->GetCharacter()->m_Pos, pKiller->GetCID(),
						m_Config.m_SvPlayerScoreSpikeTeam);
			} else {
				if (s_killer) {
					s_killer->kills_wrong++;
				} else {
					printf("couldnt find killer\n");
				}
				pKiller->m_grabs_false++;
				m_aTeamscore[TEAM_RED] += m_Config.m_SvTeamScoreSpikeFalse;
				if(pKiller->GetCharacter()) 
					GameServer()->MakeLaserTextPoints(
						pKiller->GetCharacter()->m_Pos, pKiller->GetCID(),
						m_Config.m_SvPlayerScoreSpikeFalse);
			}
			pVictim->GetPlayer()->m_RespawnTick = 
				Server()->Tick()+Server()->TickSpeed()*.5f;
		} else if(Weapon == WEAPON_SPIKE_GOLD){
			if (s_killer) {
				s_killer->kills_x2++;
			} else {
				printf("couldnt find killer\n");
			}
			pKiller->m_grabs_gold++;
			pVictim->GetPlayer()->m_deaths++;
			m_aTeamscore[pKiller->GetTeam()] += m_Config.m_SvTeamScoreSpikeGold;
			pVictim->GetPlayer()->m_RespawnTick =
				Server()->Tick()+Server()->TickSpeed()*.5f;
			if(pKiller->GetCharacter())
				GameServer()->MakeLaserTextPoints(
					pKiller->GetCharacter()->m_Pos, pKiller->GetCID(),
					m_Config.m_SvPlayerScoreSpikeGold);
		} else if(Weapon == WEAPON_HAMMER){ //only called if team mate unfreezed you
			pKiller->m_unfreeze++;
			if (s_killer && s_victim) {
				s_killer->hammers++;
				s_victim->hammered++;
				s_victim->frozeby = -1;
			} else {
				printf("couldnt find killer/victim\n");
			}
		}
	}
	
	if (Weapon == WEAPON_SPIKE_GOLD || Weapon == WEAPON_SPIKE_BLUE || 
		Weapon == WEAPON_SPIKE_RED || Weapon == WEAPON_SPIKE_NORMAL) {
		if (s_killer && s_victim) {
			const char *kname = Server()->ClientName(s_killer->id);
			int Victim = pPlVictim->GetCID();
			s_victim->deaths++;
			if (s_victim->frozeby != s_killer->id && s_victim->frozeby >= 0) {
				s_killer->steals++;
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "%s stole %s's kill!", 
					kname, Server()->ClientName(s_victim->frozeby));
				GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
			}
	
			/* handle spree */
			if (((++s_killer->spree) % 5) == 0) {
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "%s is on a spree of %d kills!", 
					kname, s_killer->spree);
				GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
			}
			if (s_victim->spree >= 5) {
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "%s's spree of %d kills ended by %s!", 
					Server()->ClientName(Victim), s_victim->spree, kname);
				GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
			}
			if (s_killer->spree > s_killer->spree_max)
				s_killer->spree_max = s_killer->spree;
			s_victim->spree = 0;
	
			/* handle multis */
			time_t ttmp = time(NULL);
			if ((ttmp - s_killer->lastkilltime) <= 5) {
				s_killer->multi++;
				if (s_killer->max_multi < s_killer->multi)
					s_killer->max_multi = s_killer->multi;
				int index = s_killer->multi - 2;
				s_killer->multis[index > 5 ? 5 : index]++;
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "%s multi x%d!", kname, s_killer->multi);
				GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
			} else {
				s_killer->multi = 1;
			}
			s_killer->lastkilltime = ttmp;	
			s_victim->frozeby = -1;	
		} else {
			printf("couldnt find killer/victim\n");
		}
	}

	if (Weapon == WEAPON_SELF) {
		pVictim->GetPlayer()->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()*.75f;
		if (s_victim) {
			s_victim->frozeby = -1;	
		} else {
			printf("couldnt find victim\n");
		}	
	} else if (Weapon == WEAPON_WORLD) {
		pVictim->GetPlayer()->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()*.75f;
		if (s_victim) {
			s_victim->frozeby = -1;
		} else {
			printf("couldnt find victim\n");
		}
	}
	
	return 0;
}

void CGameControllerFNG2::PostReset(){
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i])
		{
			GameServer()->m_apPlayers[i]->Respawn();
			GameServer()->m_apPlayers[i]->m_Score = 0;
			GameServer()->m_apPlayers[i]->ResetStats();
			GameServer()->m_apPlayers[i]->m_ScoreStartTick = Server()->Tick();
			GameServer()->m_apPlayers[i]->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
		}
	}
}

void CGameControllerFNG2::EndRound()
{
	IGameController::EndRound();
	GameServer()->SendRoundStats();
}