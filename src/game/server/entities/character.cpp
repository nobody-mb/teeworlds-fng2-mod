/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/mapitems.h>
#include <cmath>
#include "character.h"
#include "laser.h"
#include "projectile.h"
#include "../fng2define.h"
#include "../rcd.hpp"
//input count
struct CInputCount
{
	int m_Presses;
	int m_Releases;
};

CInputCount CountInput(int Prev, int Cur)
{
	CInputCount c = {0, 0};
	Prev &= INPUT_STATE_MASK;
	Cur &= INPUT_STATE_MASK;
	int i = Prev;

	while(i != Cur)
	{
		i = (i+1)&INPUT_STATE_MASK;
		if(i&1)
			c.m_Presses++;
		else
			c.m_Releases++;
	}

	return c;
}


MACRO_ALLOC_POOL_ID_IMPL(CCharacter, MAX_CLIENTS)

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld)
: CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER)
{
	m_ProximityRadius = ms_PhysSize;
	m_Health = 0;
	m_Armor = 0;
	m_Freeze.m_ActivationTick = 0;
	count = 0;
	lpi_us = li_us = 0;
	m_InvincibleTick = 0;
	m_Killer.m_KillerID = -1;
	m_Killer.m_uiKillerHookTicks = 0;
}

void CCharacter::Reset()
{
	Destroy();
}

bool CCharacter::Spawn(CPlayer *pPlayer, vec2 Pos)
{
	ccreated = time(NULL);
	m_Freeze.m_ActivationTick = 0;

	m_InvincibleTick = 0;
	m_Killer.m_KillerID = -1;
	m_Killer.m_uiKillerHookTicks = 0;

	m_EmoteStop = -1;
	m_LastAction = -1;
	m_LastNoAmmoSound = -1;
	m_LastWeapon = WEAPON_HAMMER;
	m_QueuedWeapon = -1;

	m_pPlayer = pPlayer;
	m_Pos = Pos;
	tb_aim_time = 0;
	tb_on = NULL;

	m_Core.Reset();
	m_Core.Init(&GameServer()->m_World.m_Core, GameServer()->Collision());
	m_Core.m_Pos = m_Pos;
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;

	m_ReckoningTick = 0;
	mem_zero(&m_SendCore, sizeof(m_SendCore));
	mem_zero(&m_ReckoningCore, sizeof(m_ReckoningCore));

	GameServer()->m_World.InsertEntity(this);
	m_Alive = true;

	GameServer()->m_pController->OnCharacterSpawn(this);
	//Support grenade fng here
	if (m_aWeapons[WEAPON_RIFLE].m_Got)
		m_ActiveWeapon = WEAPON_RIFLE;
	else if (m_aWeapons[WEAPON_GRENADE].m_Got)
		m_ActiveWeapon = WEAPON_GRENADE;
	else if (m_aWeapons[WEAPON_HAMMER].m_Got)
		m_ActiveWeapon = WEAPON_HAMMER;
	else if (m_aWeapons[WEAPON_GUN].m_Got)
		m_ActiveWeapon = WEAPON_GUN;

	return true;
}

void CCharacter::Destroy()
{
	if(g_Config.m_SvSmoothFreezeMode)
		GameServer()->SendTuningParams(m_pPlayer->GetCID());
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	m_Alive = false;
}

void CCharacter::force_weapon (void)
{
	m_ActiveWeapon = -1;
	///m_Alive = 0;
	//Destroy();
	//GameServer()->SendEmoticon(m_pPlayer->GetCID(), -1);
	//m_EmoteType = 1000;

	//NETOBJTYPE_GAMEINFO:
/*	CNetObj_GameInfo pObj;
	pObj.m_GameFlags = 0;
	pObj.m_GameStateFlags = 0;
	pObj.m_RoundStartTick = -1;
	pObj.m_WarmupTimer = 0;
	pObj.m_ScoreLimit = -1;
	pObj.m_TimeLimit = -1;
	pObj.m_RoundNum = 0;
	pObj.m_RoundCurrent = 0;
	*/
	
	/*CMsgPacker Msg(NETMSG_MAP_DATA);
	char blank[100] = { 1 };
	Msg.AddInt(0);
	Msg.AddInt(4);
	Msg.AddInt(0);
	Msg.AddInt(100);
	Msg.AddRaw(blank, 100);*/
	//GameServer()->Server()->m_aClients[m_pPlayer->GetCID()]->Quit();
	
	//SendMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, m_pPlayer->GetCID());

	/*CMsgPacker Msg(NETOBJTYPE_GAMEINFO);
		for(unsigned i = 0; i < 7; i++)
			Msg.AddInt(0);*/
	//Server()->SendMsg(&pObj, MSGFLAG_VITAL, m_pPlayer->GetCID());
}

void CCharacter::SetWeapon(int W)
{
	if (!IsFreezed()) {
		if (W == m_ActiveWeapon)
			return;

		m_LastWeapon = m_ActiveWeapon;
		m_QueuedWeapon = -1;
		m_ActiveWeapon = W;
		GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH);
	}
	else {
		if (W == m_LastWeapon || W == WEAPON_NINJA)
			return;

		m_LastWeapon = W;
		m_QueuedWeapon = -1;
		GameServer()->CreateSoundGlobal(SOUND_WEAPON_SWITCH, m_pPlayer->GetCID());
	}

	
	if(m_ActiveWeapon < 0 || m_ActiveWeapon >= NUM_WEAPONS)
		m_ActiveWeapon = 0;
}

bool CCharacter::IsGrounded()
{
	if(GameServer()->Collision()->CheckPoint(m_Pos.x+m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	if(GameServer()->Collision()->CheckPoint(m_Pos.x-m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	return false;
}


void CCharacter::HandleFreeze()
{
	//we basically freeze the first tick, after we actually got shot, so all input for all players is fair(pumpnetwork is after ontick)
	if(m_Freeze.m_ActivationTick != 0 && m_Freeze.m_ActivationTick != Server()->Tick() && m_ActiveWeapon != WEAPON_NINJA) {
		//do actual freezing
		m_ActiveWeapon = WEAPON_NINJA;
		ResetInput();
	}
	
	if(m_ActiveWeapon != WEAPON_NINJA || !IsAlive())
		return;


	if ((Server()->Tick() - m_Freeze.m_ActivationTick) % Server()->TickSpeed() == 0) {
		GameServer()->CreateDamageInd(m_Pos, 0, (m_Freeze.m_Duration - (Server()->Tick() - m_Freeze.m_ActivationTick) / Server()->TickSpeed()), m_pPlayer->GetTeam(), m_pPlayer->GetCID());
	}

	if ((Server()->Tick() - m_Freeze.m_ActivationTick) > (m_Freeze.m_Duration * Server()->TickSpeed()))
	{
		Unfreeze(-1);

		m_InvincibleTick = Server()->TickSpeed()*0.5;
		return;
	}

	// force freeze
	SetWeapon(WEAPON_NINJA);
	return;
}


void CCharacter::DoWeaponSwitch()
{
	// make sure we can switch
	if(m_ReloadTimer != 0 || m_QueuedWeapon == -1)
		return;

	// switch Weapon
	SetWeapon(m_QueuedWeapon);
}

void CCharacter::HandleWeaponSwitch()
{
	int WantedWeapon = m_ActiveWeapon;
	if(m_QueuedWeapon != -1)
		WantedWeapon = m_QueuedWeapon;

	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(Next < 128) // make sure we only try sane stuff
	{
		while(Next) // Next Weapon selection
		{
			WantedWeapon = (WantedWeapon+1)%NUM_WEAPONS;
			if(m_aWeapons[WantedWeapon].m_Got)
				Next--;
		}
	}

	if(Prev < 128) // make sure we only try sane stuff
	{
		while(Prev) // Prev Weapon selection
		{
			WantedWeapon = (WantedWeapon-1)<0?NUM_WEAPONS-1:WantedWeapon-1;
			if(m_aWeapons[WantedWeapon].m_Got)
				Prev--;
		}
	}

	// Direct Weapon selection
	if(m_LatestInput.m_WantedWeapon)
		WantedWeapon = m_Input.m_WantedWeapon-1;

	// check for insane values
	if(WantedWeapon >= 0 && WantedWeapon < NUM_WEAPONS && WantedWeapon != m_ActiveWeapon && m_aWeapons[WantedWeapon].m_Got)
		m_QueuedWeapon = WantedWeapon;

	DoWeaponSwitch();
}

bool CCharacter::IsFreezed(){
	return m_Freeze.m_ActivationTick != 0;
}

void CCharacter::anti_triggerbot (void)
{
	char aBuf[128] = { 0 };
	long delay = get_time_us() - tb_aim_time;
	if (delay < 1000000) {
		CPlayer *p;	
		if ((p = GetPlayer())) {
			p->tb_avg = ((p->tb_avg * p->tb_num) + delay) / 
				    (++p->tb_num);
			if (delay < 10)
				p->tb_under10++;
			if (delay < 60000)
				p->tb_under100k++;
			float perc1 = ((float)p->tb_under10 / 
				((float)p->tb_num)); 
			float perc = ((float)p->tb_under100k / 
				((float)p->tb_num)); 
			printf("** %s %6ld (avg %ld)\t of %d: %.02f%% <10, %.02f%% <60k\n", 
				ID_NAME(GetPlayer()->GetCID()), delay, 
				p->tb_avg, p->tb_num, perc1, perc);	 
			if ((p->tb_num > 10 && perc1 > 0.9) ||
			    (p->tb_num > 20 && perc1 > 0.5) || 
			    (p->tb_num > 40 && perc1 > 0.35)) {
				str_format(aBuf, sizeof(aBuf), 
				"%s possible triggerbot (%.02f%% %d 10)", 
				ID_NAME(GetPlayer()->GetCID()), perc1, p->tb_num);
				GameServer()->SendChat(-1, 
					CGameContext::CHAT_ALL, aBuf);
				count = 1;
			} 					
			if ((p->tb_num > 10 && perc > 0.99) ||
			    (p->tb_num > 40 && perc > 0.7)) {
				str_format(aBuf, sizeof(aBuf), 
				"%s possible triggerbot (%.02f%% %d 60k)", 
				ID_NAME(GetPlayer()->GetCID()), perc, p->tb_num);
				GameServer()->SendChat(-1, 
					CGameContext::CHAT_ALL, aBuf);
				count = 1;
			} 
		}

		str_format(aBuf, sizeof(aBuf), "%08ld%s\n", delay, 
			ID_NAME(GetPlayer()->GetCID()));

		int fd;
		if ((fd = open("delay.txt", O_RDWR|O_CREAT|O_APPEND, 0777)) < 0)
			perror("open");
		else
			if (write(fd, aBuf, strlen(aBuf)) != strlen(aBuf))
				perror("write");		
		close(fd);
		
	}
}

void CCharacter::FireWeapon()
{
	if(m_ReloadTimer != 0 || (IsFreezed() && m_Freeze.m_ActivationTick != Server()->Tick()))
		return;

	DoWeaponSwitch();
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	bool FullAuto = false;
	if(m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_RIFLE)
		FullAuto = true;
	//RajhCheatDetector::OnFire(m_pPlayer);

	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;

	if(FullAuto && (m_LatestInput.m_Fire&1) && m_aWeapons[m_ActiveWeapon].m_Ammo)
		WillFire = true;

	if(!WillFire)
		return;

	// check for ammo
	if(!m_aWeapons[m_ActiveWeapon].m_Ammo)
	{
		printf("%s firing without ammo\n", 
			Server()->ClientName(m_pPlayer->GetCID()));
		anti_triggerbot();
		// 125ms is a magical limit of how fast a human can click
		m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
		if(m_LastNoAmmoSound+Server()->TickSpeed() <= Server()->Tick())
		{
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
			m_LastNoAmmoSound = Server()->Tick();
		}
		return;
	}

	vec2 ProjStartPos = m_Pos+Direction*m_ProximityRadius*0.75f;

	switch(m_ActiveWeapon)
	{
		case WEAPON_HAMMER:
		{
			GameServer()->CreateSound(m_Pos, SOUND_HAMMER_FIRE);

			CCharacter *apEnts[MAX_CLIENTS];
			int Hits = 0;
			int Num = GameServer()->m_World.FindEntities(ProjStartPos, m_ProximityRadius*0.5f, (CEntity**)apEnts,
														MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				CCharacter *pTarget = apEnts[i];

				if ((pTarget == this) || GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->m_Pos, NULL, NULL))
					continue;

				// set his velocity to fast upward (for now)
				if(length(pTarget->m_Pos-ProjStartPos) > 0.0f)
					GameServer()->CreateHammerHit(pTarget->m_Pos-normalize(pTarget->m_Pos-ProjStartPos)*m_ProximityRadius*0.5f);
				else
					GameServer()->CreateHammerHit(ProjStartPos);

				pTarget->TakeHammerHit(this);
				Hits++;
			}

			// if we Hit anything, we have to wait for the reload
			if(Hits)
				m_ReloadTimer = Server()->TickSpeed()/3;

		} break;

		case WEAPON_GUN:
		{
			CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GUN,
				m_pPlayer->GetCID(),
				ProjStartPos,
				Direction,
				(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
				1, 0, 0, -1, WEAPON_GUN);

			GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
		} break;

		case WEAPON_SHOTGUN:
		{
			int ShotSpread = 2;

			for(int i = -ShotSpread; i <= ShotSpread; ++i)
			{
				float Spreading[] = {-0.185f, -0.070f, 0, 0.070f, 0.185f};
				float a = GetAngle(Direction);
				a += Spreading[i+2];
				float v = 1-(absolute(i)/(float)ShotSpread);
				float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
				CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
					m_pPlayer->GetCID(),
					ProjStartPos,
					vec2(cosf(a), sinf(a))*Speed,
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_ShotgunLifetime),
					1, 0, 0, -1, WEAPON_SHOTGUN);
			}

			GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);
		} break;

		case WEAPON_GRENADE:
		{
			CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GRENADE,
				m_pPlayer->GetCID(),
				ProjStartPos,
				Direction,
				(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
				1, true, 0, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);

			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
		} break;

		case WEAPON_RIFLE:
		{
			anti_triggerbot();
			if ((time(NULL) != ccreated)) {	/* dont count spawn shot */
				struct tee_stats *tmp;
				if ((tmp = GameServer()->m_pController->t_stats->
				           current[m_pPlayer->GetCID()])) {
					tmp->shots++;
				} else {
					printf("couldnt +1 %s\n", Server()->
						ClientName(m_pPlayer->GetCID()));
				}
			}
			if (!count) {
				new CLaser(GameWorld(), m_Pos, Direction, 
				           GameServer()->Tuning()->m_LaserReach, 
				           m_pPlayer->GetCID());
			}
			GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
		} break;

		case WEAPON_NINJA:
		{
		} break;

	}

	m_AttackTick = Server()->Tick();

	if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0) // -1 == unlimited
		m_aWeapons[m_ActiveWeapon].m_Ammo--;

	if(!m_ReloadTimer)
		m_ReloadTimer = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Firedelay * Server()->TickSpeed() / 1000;
}

void CCharacter::HandleWeapons()
{
	// check reload timer
	if(m_ReloadTimer)
	{
		m_ReloadTimer--;
		return;
	}

	// fire Weapon, if wanted
	FireWeapon();

	// ammo regen
	int AmmoRegenTime = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Ammoregentime;
	if(AmmoRegenTime)
	{
		// If equipped and not active, regen ammo?
		if (m_ReloadTimer <= 0)
		{
			if (m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart < 0)
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = Server()->Tick();

			if ((Server()->Tick() - m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart) >= AmmoRegenTime * Server()->TickSpeed() / 1000)
			{
				// Add some ammo
				m_aWeapons[m_ActiveWeapon].m_Ammo = min(m_aWeapons[m_ActiveWeapon].m_Ammo + 1, 5);
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
			}
		}
		else
		{
			m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
		}
	}

	return;
}

bool CCharacter::GiveWeapon(int Weapon, int Ammo)
{
	if(m_aWeapons[Weapon].m_Ammo < g_pData->m_Weapons.m_aId[Weapon].m_Maxammo || !m_aWeapons[Weapon].m_Got)
	{
		m_aWeapons[Weapon].m_Got = true;
		m_aWeapons[Weapon].m_Ammo = min(g_pData->m_Weapons.m_aId[Weapon].m_Maxammo, Ammo);
		return true;
	}
	return false;
}

void CCharacter::Freeze(int TimeInSec) {
	if(!IsFreezed())m_LastWeapon = m_ActiveWeapon;
	m_Freeze.m_ActivationTick = Server()->Tick();
	m_Freeze.m_Duration = TimeInSec;
	
	if(g_Config.m_SvSmoothFreezeMode)
		GameServer()->SendFakeTuningParams(m_pPlayer->GetCID());
}

void CCharacter::Unfreeze(int pPlayerID) {
	//Set Weapon to old one, and unfreeze
	m_ActiveWeapon = m_LastWeapon;
	m_Freeze.m_ActivationTick = 0;
	m_Killer.m_uiKillerHookTicks = 0;
	m_Killer.m_KillerID = m_pPlayer->GetCID();

	if (pPlayerID != -1) {
		int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, GameServer()->m_apPlayers[pPlayerID], WEAPON_HAMMER);
		// send the kill message
		CNetMsg_Sv_KillMsg Msg;
		Msg.m_Killer = pPlayerID;
		Msg.m_Victim = m_pPlayer->GetCID();
		Msg.m_Weapon = WEAPON_HAMMER;
		Msg.m_ModeSpecial = ModeSpecial;
		GameServer()->SendPackMsg(&Msg, MSGFLAG_VITAL);
	}
		
	if(g_Config.m_SvSmoothFreezeMode)
		GameServer()->SendTuningParams(m_pPlayer->GetCID());
}

void CCharacter::SetEmote(int Emote, int Tick)
{
	m_EmoteType = Emote;
	m_EmoteStop = Tick;
}

void CCharacter::SetKiller(int pKillerID, unsigned int pHookTicks) {
	if (m_Killer.m_uiKillerHookTicks < pHookTicks) {
		m_Killer.m_uiKillerHookTicks = pHookTicks;
		m_Killer.m_KillerID = pKillerID;
	}
}

void CCharacter::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{
	if (IsFreezed()) {
		pNewInput->m_Direction = 0;
		pNewInput->m_Jump = 0;
		pNewInput->m_Fire = 0;
		pNewInput->m_Hook = 0;
	}

	// check for changes
	if(mem_comp(&m_Input, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();

	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));
	m_NumInputs++;

	// it is not allowed to aim in the center
	if(m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;
}

#include <sys/time.h>
long CCharacter::get_time_us (void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000000) + tv.tv_usec;
}

void CCharacter::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	lpi_us = li_us;
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	li_us = get_time_us();
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));
	
	//float disc = distance(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY),
	//		vec2(m_LatestPrevInput.m_TargetX, m_LatestPrevInput.m_TargetY));
	//printf("mouse traveled %f in %d ticks (%f)\n", disc, num_bt, disc / num_bt);
	//disc /= num_bt;
	//num_bt = 0;

	// it is not allowed to aim in the center
	if(m_LatestInput.m_TargetX == 0 && m_LatestInput.m_TargetY == 0)
		m_LatestInput.m_TargetY = -1;

	//antibot test	
	vec2 At;
	CCharacter *tmpc;
	vec2 Direction = m_Pos + (normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY)) *
		 GameServer()->Tuning()->m_LaserReach);
	tmpc = GameServer()->m_World.IntersectCharacter(m_Pos, Direction, 0.f, At, this);
	if (tmpc && tmpc != tb_on) {
		tb_aim_time = get_time_us();
	}
	tb_on = tmpc;
	
	/*if (tb_aim_time && g_Config.m_RcdEnable) {
		printf("%s aiming at target at %ld\n", 
			Server()->ClientName(m_pPlayer->GetCID()),
			tb_aim_time);
	}
	*/
	if(m_NumInputs > 2 && m_pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}
	
	//AntiBot by TsFreddie ------------------------------------------------------------
	vec2 TarPos = vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY);
 	CCharacter *aEnts[MAX_CLIENTS];
 	int Num = GameServer()->m_World.FindEntities(m_Pos + TarPos, 10, (CEntity**)aEnts,
 		MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	char aBuf[255];
	// Dyn:632 Normal:399
	//vec2 TarPos = vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY);
	float TarPosLength = length(TarPos);
	float TravelDis = distance(m_ABSpinPos,TarPos);
	
	m_last_travel_dist = TravelDis;
	m_last_tarposlen = TarPosLength;
	
	if (TarPosLength < 398 || (TarPosLength > 401 && TarPosLength < 632)) {
		if (TravelDis > 50) {
			if (abs(m_ABSpinLength - TarPosLength) < 1)
				m_ABSpinTime ++;
			else
				m_ABSpinTime = 0;
			m_ABSpinLength = TarPosLength;
			m_ABSpinPos = TarPos;
		}
	} else {
		if (TarPosLength > 634 && !m_pPlayer->GetBot(1)) {
			m_pPlayer->SetBot(1);
			str_format(aBuf, sizeof(aBuf), 
				"%s is AimBot(Invaild Mouse Pos len %f dist %f)", 
				Server()->ClientName(m_pPlayer->GetCID()), TarPosLength,
					 TravelDis);
			GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
		}
	}
	//m_aim_dist = 99999;
	for (int i = 0; i < Num; ++i)
	{
		if (aEnts[i] == this)
 			continue;
 		float CheckAimDis = distance(m_Pos + TarPos, aEnts[i]->m_Pos);
 		/*if (CheckAimDis < 5) {
 			float teedis = distance(m_Pos, aEnts[i]->m_Pos);
			str_format(aBuf, sizeof(aBuf), "%s¶%f¶%f¶%f¶aim\n",
				ID_NAME(m_pPlayer->GetCID()), CheckAimDis, teedis, disc);
			printf("%s", aBuf);
			int fd;
			if ((fd = open("dist.txt", O_RDWR|O_CREAT|O_APPEND, 0777)) < 0)
				perror("open");
			else
				if (write(fd, aBuf, strlen(aBuf)) != strlen(aBuf))
					perror("write");		
			close(fd);

 			m_aim_dist = CheckAimDis;
 			//printf("dist %f %s", m_aim_dist, ID_NAME(m_pPlayer->GetCID()));
 		}*/
 		//GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
 		if (CheckAimDis < 1)
 			m_ABAimAcTime ++;
 		else
 			m_ABAimAcTime = 0;
 	}
 	if ((m_ABSpinTime == 10) && !m_pPlayer->GetBot(0))
	{
		m_pPlayer->SetBot(0);
		str_format(aBuf, sizeof(aBuf), "%s is spinning (%d)", 
			Server()->ClientName(m_pPlayer->GetCID()), 
			(int)(m_ABSpinLength + 0.5));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
	}
	if ((m_ABAimAcTime == 5) && !m_pPlayer->GetBot(1))
 	{
 		m_pPlayer->SetBot(1);
 		str_format(aBuf, sizeof(aBuf), "%s is AimBot(Position matching) %f", 
 			Server()->ClientName(m_pPlayer->GetCID()), m_ABSpinLength);
 		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
 	}
 	if ((m_ABAimTime == 10) && !m_pPlayer->GetBot(1))
 	{
 		m_pPlayer->SetBot(1);
 		str_format(aBuf, sizeof(aBuf), "%s is AimBot(Similar behavior) %f", 
 			Server()->ClientName(m_pPlayer->GetCID()), m_ABSpinLength);
  		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
  	}
	
	if (m_pPlayer && Server()->Tick() > m_ABNextBanTick && m_pPlayer->GetBot(1))
	{
		struct tee_stats *tmp = GameServer()->m_pController->t_stats->
			current[m_pPlayer->GetCID()];
		if (tmp && ++tmp->is_bot < 2) {
			str_format(aBuf, sizeof(aBuf), "Voting to ban %s.",Server()->
				ClientName(m_pPlayer->GetCID()));
			GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
			str_format(aBuf, sizeof(aBuf), "Ban %s",Server()->
				ClientName(m_pPlayer->GetCID()));
			char aCmd[128];
			str_format(aCmd, sizeof(aCmd), "Ban %d 60 Bot Detected!",
				m_pPlayer->GetCID());
			GameServer()->StartVote(aBuf, aCmd, "Bot Detected![By System]");
			m_ABNextBanTick = Server()->Tick() + Server()->TickSpeed() * 60;
		}
	}
	// -------------------------------------------------------------------------------------
/*	
#ifndef __APPLE__
	const int twac = m_AntiCheats.CheckInputs(Server()->Tick(), m_LatestInput, 
		m_LatestPrevInput);
	if (twac) {
		dbg_msg("ANTI-CHEAT", "Detected! %d", twac);
		char buf[256] = { 0 };
		snprintf(buf, sizeof(buf), "%s %d", 
			ID_NAME(m_pPlayer->GetCID()), twac);
		if (twac != 2)
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, buf);
	}
#endif*/
	mem_copy(&OldInput, &m_LatestPrevInput, sizeof(m_LatestPrevInput));
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::ResetInput()
{
	m_Input.m_Direction = 0;
	m_Input.m_Hook = 0;
	// simulate releasing the fire button
	if((m_Input.m_Fire&1) != 0)
		m_Input.m_Fire++;
	m_Input.m_Fire &= INPUT_STATE_MASK;
	m_Input.m_Jump = 0;
	m_LatestPrevInput = m_LatestInput = m_Input;
}

void CCharacter::Tick()
{
	//num_bt++;
	if (m_InvincibleTick > 0) --m_InvincibleTick;

	if(m_pPlayer->m_ForceBalanced)
	{
		char Buf[128];
		str_format(Buf, sizeof(Buf), "You were moved to %s due to team balancing", GameServer()->m_pController->GetTeamName(m_pPlayer->GetTeam()));
		GameServer()->SendBroadcast(Buf, m_pPlayer->GetCID());

		m_pPlayer->m_ForceBalanced = false;
	}

	//before the core update to know if we hooked somebody before it.
	int iHookedPlayer = m_Core.m_HookedPlayer;

	m_Core.m_Input = m_Input;
	m_Core.Tick(true);

	// handle death-tiles and leaving gamelayer
	if(GameServer()->Collision()->GetCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameLayerClipped(m_Pos))
	{
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
	}

	unsigned char flag = 0;
	if ((flag |= GameServer()->Collision()->GetCollisionAt(m_Pos.x + m_ProximityRadius / 3.f, m_Pos.y - m_ProximityRadius / 3.f))& (CCollision::COLFLAG_SPIKE_NORMAL | CCollision::COLFLAG_SPIKE_RED | CCollision::COLFLAG_SPIKE_BLUE | CCollision::COLFLAG_SPIKE_GOLD) ||
		(flag |= GameServer()->Collision()->GetCollisionAt(m_Pos.x + m_ProximityRadius / 3.f, m_Pos.y + m_ProximityRadius / 3.f))& (CCollision::COLFLAG_SPIKE_NORMAL | CCollision::COLFLAG_SPIKE_RED | CCollision::COLFLAG_SPIKE_BLUE | CCollision::COLFLAG_SPIKE_GOLD) ||
		(flag |= GameServer()->Collision()->GetCollisionAt(m_Pos.x - m_ProximityRadius / 3.f, m_Pos.y - m_ProximityRadius / 3.f))& (CCollision::COLFLAG_SPIKE_NORMAL | CCollision::COLFLAG_SPIKE_RED | CCollision::COLFLAG_SPIKE_BLUE | CCollision::COLFLAG_SPIKE_GOLD) ||
		(flag |= GameServer()->Collision()->GetCollisionAt(m_Pos.x - m_ProximityRadius / 3.f, m_Pos.y + m_ProximityRadius / 3.f))& (CCollision::COLFLAG_SPIKE_NORMAL | CCollision::COLFLAG_SPIKE_RED | CCollision::COLFLAG_SPIKE_BLUE | CCollision::COLFLAG_SPIKE_GOLD))
	{
		DieSpikes(m_Killer.m_KillerID, flag);
	}

	//HandleFreeze
	HandleFreeze();

	// handle Weapons
	HandleWeapons();

	// Previnput
	m_PrevInput = m_Input;

	if (iHookedPlayer != -1 && g_Config.m_SvKillTakeOverTime != -1) {
		CPlayer *pPlayer = GameServer()->m_apPlayers[iHookedPlayer];
		if (pPlayer) {
			//if the hooker is not in the team of the hooked player, we check how long "we" hooked the hooked player already
			if (pPlayer->GetTeam() != m_pPlayer->GetTeam() || !GameServer()->m_pController->IsTeamplay()) {
				//if the player is hooked over 0.25 seconds, this character is "allowed" to call himself the killer of the hooked player
				if (m_Core.m_HookTick >= Server()->TickSpeed() / (1000.f / (float)g_Config.m_SvKillTakeOverTime)) {
					CCharacter* pChr = pPlayer->GetCharacter();
					if (pChr) {
						//if we stop hook the hooked player, we have 0 hook ticks on the target
						if (m_Core.m_HookState == HOOK_RETRACTED && pChr->m_Killer.m_KillerID == m_pPlayer->GetCID()) {
							pChr->m_Killer.m_uiKillerHookTicks = 0;
						}
						// else we "try" to set us as killer
						else pChr->SetKiller(m_pPlayer->GetCID(), m_Core.m_HookTick);
					}
				}
			}
			else {
				//if we hook someone from the same team, we are the killer, if he falls into spikes
				CCharacter* pChr = pPlayer->GetCharacter();
				if (pChr) {
					pChr->m_Killer.m_uiKillerHookTicks = 0;
					pChr->m_Killer.m_KillerID = iHookedPlayer;
				}
			}
		}
	}
	/*
	if(CountInput(m_LatestPrevInput.m_Hook, m_LatestInput.m_Hook).m_Presses)
		RajhCheatDetector::OnFire(m_pPlayer);
	*/
	int events = m_Core.m_TriggeredEvents;
	int teamhook = 0;
	if (GetPlayer() && (events & COREEVENT_HOOK_ATTACH_PLAYER) && m_Core.m_HookedPlayer != -1) {
		CPlayer *pPlayer = GameServer()->m_apPlayers[m_Core.m_HookedPlayer];
		if (GameServer()->m_pController->IsTeamplay()) {
			if (pPlayer && m_pPlayer->GetTeam() == pPlayer->GetTeam()) {
				teamhook = 1;
			}
		}
		float td = 0;
		float ds = distance(vec2(m_Input.m_TargetX, m_Input.m_TargetY),
			vec2(OldInput.m_TargetX, OldInput.m_TargetY));
		if (pPlayer->GetCharacter()) {
			td = distance(m_Pos, pPlayer->GetCharacter()->m_Pos);
		}
		
		char aBuf[128];
		snprintf(aBuf, sizeof(aBuf), 
			" - %s%shooked %s at %f | mouse traveled %f in %ld us\n", 
			ID_NAME(GetPlayer()->GetCID()), (teamhook ? " team" : " "), 
			ID_NAME(m_Core.m_HookedPlayer), td, ds, li_us - lpi_us);
		
		if (g_Config.m_RcdEnable)
			GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
	

		str_format(aBuf, sizeof(aBuf), "%08ld%08ld%08ld%c%s\n", (long)(ds), 
			(long)td, li_us - lpi_us, (teamhook ? '+' : '-'), 
			ID_NAME(GetPlayer()->GetCID()));

		int fd;
		if ((fd = open("hook.txt", O_RDWR|O_CREAT|O_APPEND, 0777)) < 0)
			perror("open");
		else
			if (write(fd, aBuf, strlen(aBuf)) != strlen(aBuf))
				perror("write");		
		close(fd);
		//RajhCheatDetector::OnHit(m_pPlayer, m_Core.m_HookedPlayer);*/
		
	}
	return;
}

void CCharacter::TickDefered()
{
	// advance the dummy
	{
		CWorldCore TempWorld;
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision());
		m_ReckoningCore.Tick(false);
		m_ReckoningCore.Move();
		m_ReckoningCore.Quantize();
	}

	//lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));

	m_Core.Move();
	bool StuckAfterMove = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Core.Quantize();
	bool StuckAfterQuant = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Pos = m_Core.m_Pos;

	if(!StuckBefore && (StuckAfterMove || StuckAfterQuant))
	{
		// Hackish solution to get rid of strict-aliasing warning
		union
		{
			float f;
			unsigned u;
		}StartPosX, StartPosY, StartVelX, StartVelY;

		StartPosX.f = StartPos.x;
		StartPosY.f = StartPos.y;
		StartVelX.f = StartVel.x;
		StartVelY.f = StartVel.y;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x",
			StuckBefore,
			StuckAfterMove,
			StuckAfterQuant,
			StartPos.x, StartPos.y,
			StartVel.x, StartVel.y,
			StartPosX.u, StartPosY.u,
			StartVelX.u, StartVelY.u);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	int Events = m_Core.m_TriggeredEvents;
	QuadroMask Mask = CmaskAllExceptOne(m_pPlayer->GetCID());

	if(Events&COREEVENT_GROUND_JUMP) GameServer()->CreateSound(m_Pos, SOUND_PLAYER_JUMP, Mask);

	if(Events&COREEVENT_HOOK_ATTACH_PLAYER) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_PLAYER, CmaskAll());
	if(Events&COREEVENT_HOOK_ATTACH_GROUND) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_GROUND, Mask);
	if(Events&COREEVENT_HOOK_HIT_NOHOOK) GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH, Mask);


	if(m_pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		m_Pos.x = m_Input.m_TargetX;
		m_Pos.y = m_Input.m_TargetY;
	}

	// update the m_SendCore if needed
	{
		CNetObj_Character Predicted;
		CNetObj_Character Current;
		mem_zero(&Predicted, sizeof(Predicted));
		mem_zero(&Current, sizeof(Current));
		m_ReckoningCore.Write(&Predicted);
		m_Core.Write(&Current);

		// only allow dead reackoning for a top of 3 seconds
		if(m_ReckoningTick+Server()->TickSpeed()*3 < Server()->Tick() || mem_comp(&Predicted, &Current, sizeof(CNetObj_Character)) != 0)
		{
			m_ReckoningTick = Server()->Tick();
			m_SendCore = m_Core;
			m_ReckoningCore = m_Core;
		}
	}
}

void CCharacter::TickPaused()
{
	++m_AttackTick;
	++m_Freeze.m_ActivationTick;
	++m_ReckoningTick;
	if(m_LastAction != -1)
		++m_LastAction;
	if(m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart > -1)
		++m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart;
	if(m_EmoteStop > -1)
		++m_EmoteStop;
}

bool CCharacter::IncreaseHealth(int Amount)
{
	if(m_Health >= 10)
		return false;
	m_Health = clamp(m_Health+Amount, 0, 10);
	return true;
}

bool CCharacter::IncreaseArmor(int Amount)
{
	if(m_Armor >= 10)
		return false;
	m_Armor = clamp(m_Armor+Amount, 0, 10);
	return true;
}


void CCharacter::Die(int Killer, int Weapon)
{
	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick() + Server()->TickSpeed() / 2;
	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, GameServer()->m_apPlayers[Killer], Weapon);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "kill killer='%d:%s' victim='%d:%s' weapon=%d special=%d",
		Killer, Server()->ClientName(Killer),
		m_pPlayer->GetCID(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	// send the kill message
	CNetMsg_Sv_KillMsg Msg;
	Msg.m_Killer = Killer;
	Msg.m_Victim = m_pPlayer->GetCID();
	Msg.m_Weapon = Weapon;
	Msg.m_ModeSpecial = ModeSpecial;
	GameServer()->SendPackMsg(&Msg, MSGFLAG_VITAL);

	// a nice sound
	GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE);
	if(g_Config.m_SvSmoothFreezeMode)
		GameServer()->SendTuningParams(m_pPlayer->GetCID());

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	m_Alive = false;
	GameServer()->m_World.RemoveEntity(this);
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());
}

void CCharacter::DieSpikes(int pPlayerID, unsigned char spikes_flag) {
	int Weapon = 0;


	if (spikes_flag&CCollision::COLFLAG_SPIKE_NORMAL) 		Weapon = WEAPON_SPIKE_NORMAL;
	else if (spikes_flag&CCollision::COLFLAG_SPIKE_RED)		Weapon = WEAPON_SPIKE_RED;
	else if (spikes_flag&CCollision::COLFLAG_SPIKE_BLUE)	Weapon = WEAPON_SPIKE_BLUE;
	else if (spikes_flag&CCollision::COLFLAG_SPIKE_GOLD)	Weapon = WEAPON_SPIKE_GOLD;

	// if the player leaves the game, he will be nullptr and we handle it like a selfkill
	if (pPlayerID == -1 || GameServer()->m_apPlayers[pPlayerID] == 0) pPlayerID = m_pPlayer->GetCID();

	if (!IsFreezed() || pPlayerID == m_pPlayer->GetCID()) Weapon = WEAPON_WORLD;

	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, GameServer()->m_apPlayers[pPlayerID], Weapon);

	//if needed the mod can tell the character to not die with modespecial = -1
	if(ModeSpecial >= 0){
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "kill spikes killer='%d:%s' victim='%d:%s' weapon=%d special=%d",
			pPlayerID, Server()->ClientName(pPlayerID),
			m_pPlayer->GetCID(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

		if (IsFreezed() && pPlayerID != m_pPlayer->GetCID()) {
			// send the kill message
			CNetMsg_Sv_KillMsg Msg;
			Msg.m_Killer = pPlayerID;
			Msg.m_Victim = m_pPlayer->GetCID();
			Msg.m_Weapon = WEAPON_NINJA;
			Msg.m_ModeSpecial = ModeSpecial;
			GameServer()->SendPackMsg(&Msg, MSGFLAG_VITAL);

			if (GameServer()->m_pController->IsTeamplay() && IsFalseSpike(GameServer()->m_apPlayers[pPlayerID]->GetTeam(), spikes_flag)) {
				CCharacter* pKiller = ((CPlayer*)GameServer()->m_apPlayers[pPlayerID])->GetCharacter();
				if (pKiller && !pKiller->IsFreezed()) {
					pKiller->Freeze(g_Config.m_SvFalseSpikeFreeze);
					GameServer()->CreateSound(pKiller->m_Pos, SOUND_TEE_CRY);
				}
			}
			else {
				//set attacker's face to happy (taunt!)
				CCharacter* pKiller = ((CPlayer*)GameServer()->m_apPlayers[pPlayerID])->GetCharacter();
				if (pKiller)
				{
					pKiller->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
				}

				if (GameServer()->m_pController->IsTeamplay()) {
					int team = GameServer()->m_apPlayers[pPlayerID]->GetTeam();
					GameServer()->CreateSoundTeam(m_Pos, SOUND_CTF_CAPTURE, team, pPlayerID);
					GameServer()->CreateSoundTeam(m_Pos, SOUND_CTF_GRAB_PL, (team == TEAM_BLUE) ? TEAM_RED : TEAM_BLUE, m_pPlayer->GetCID());
				} else {
					GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE, pPlayerID);
					GameServer()->CreateSoundGlobal(SOUND_CTF_GRAB_PL, m_pPlayer->GetCID());			
				}
			}
		}
		//if not freezed or selfkill
		else {
			// send the kill message
			CNetMsg_Sv_KillMsg Msg;
			Msg.m_Killer = m_pPlayer->GetCID();
			Msg.m_Victim = m_pPlayer->GetCID();
			Msg.m_Weapon = WEAPON_WORLD;
			Msg.m_ModeSpecial = ModeSpecial;
			GameServer()->SendPackMsg(&Msg, MSGFLAG_VITAL);
		}

		// a nice sound
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE);
		if(g_Config.m_SvSmoothFreezeMode)
			GameServer()->SendTuningParams(m_pPlayer->GetCID());

		// this is for auto respawn after 3 secs
		m_pPlayer->m_DieTick = Server()->Tick();

		GameServer()->m_World.RemoveEntity(this);
		Destroy();
		GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());
	}

}

bool CCharacter::IsFalseSpike(int Team, unsigned char spike_flag) {
	if (Team == TEAM_BLUE && (spike_flag&CCollision::COLFLAG_SPIKE_RED) != 0) return true;
	else if (Team == TEAM_RED && (spike_flag&CCollision::COLFLAG_SPIKE_BLUE) != 0) return true;
	return false;
}

void CCharacter::Hit(int Killer, int Weapon)
{
	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, GameServer()->m_apPlayers[Killer], Weapon);
	
	m_Killer.m_uiKillerHookTicks = 0;
	m_Killer.m_KillerID = Killer;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "hit killer='%d:%s' victim='%d:%s' weapon=%d special=%d",
		Killer, Server()->ClientName(Killer),
		m_pPlayer->GetCID(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	// send the kill message
	CNetMsg_Sv_KillMsg Msg;
	Msg.m_Killer = Killer;
	Msg.m_Victim = m_pPlayer->GetCID();
	Msg.m_Weapon = Weapon;
	Msg.m_ModeSpecial = ModeSpecial;
	GameServer()->SendPackMsg(&Msg, MSGFLAG_VITAL);

	GameServer()->CreateSoundGlobal(SOUND_HIT, Killer);
	GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());
}

bool CCharacter::TakeDamage(vec2 Force, int Dmg, int From, int Weapon)
{
	m_Core.m_Vel += Force;

	CPlayer *pPlayer = GameServer()->m_apPlayers[From];

	if ((Weapon == WEAPON_RIFLE || Weapon == WEAPON_GRENADE) && !IsFreezed()) {
		if ((GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From) && !g_Config.m_SvTeamdamage) || From == m_pPlayer->GetCID()) {
			if (From == m_pPlayer->GetCID()) {
				//don't loose nades, when hitting yourself
				if(m_aWeapons[WEAPON_GRENADE].m_Got) m_aWeapons[WEAPON_GRENADE].m_Ammo = min(m_aWeapons[m_ActiveWeapon].m_Ammo + 1, 5);
			}
			return false;
		}
		
		//this is better for grenade fng
		if(Weapon == WEAPON_GRENADE && Dmg < g_Config.m_SvGrenadeDamageToHit) return false;

		if (m_InvincibleTick == 0) {
			Freeze(g_Config.m_SvHitFreeze);
			Hit(From, Weapon);

			//set attacker's face to happy (taunt!)
			if (From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
			{
				CCharacter *pChr = pPlayer->GetCharacter();
				if (pChr)
				{
					pChr->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
				}
			}

			GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);

			m_EmoteType = EMOTE_PAIN;
			m_EmoteStop = Server()->Tick() + 500 * Server()->TickSpeed() / 1000;
		}
	}
	//RajhCheatDetector::OnHit(GameServer()->m_apPlayers[From], m_pPlayer->GetCID());
	return true;
}


void CCharacter::TakeHammerHit(CCharacter* pFrom)
{
	vec2 Dir;
	if (length(m_Pos - pFrom->m_Pos) > 0.0f)
		Dir = normalize(m_Pos - pFrom->m_Pos);
	else
		Dir = vec2(0.f, -1.f);

	vec2 Push = vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f;
	if (GameServer()->m_pController->IsTeamplay() && pFrom->GetPlayer() && m_pPlayer->GetTeam() == pFrom->GetPlayer()->GetTeam() && IsFreezed()) {
		Push.x *= g_Config.m_SvMeltHammerScaleX*0.01f;
		Push.y *= g_Config.m_SvMeltHammerScaleY*0.01f;
	}
	else {
		Push.x *= g_Config.m_SvHammerScaleX*0.01f;
		Push.y *= g_Config.m_SvHammerScaleY*0.01f;
	}

	m_Core.m_Vel += Push;

	CPlayer* pPlayer = pFrom->GetPlayer();
	if (pPlayer && (GameServer()->m_pController->IsTeamplay() && pPlayer->GetTeam() == m_pPlayer->GetTeam())) {
		m_Killer.m_uiKillerHookTicks = 0;
		m_Killer.m_KillerID = m_pPlayer->GetCID();
		if (IsFreezed()) {
			if (((float)m_Freeze.m_Duration - (float)(Server()->Tick() - m_Freeze.m_ActivationTick - 1) / (float)Server()->TickSpeed()) < 3.f) {
				Unfreeze(pPlayer->GetCID());

				/*vec2 dir = m_Pos;
				if (pPlayer->GetCharacter()) {
				dir -= pPlayer->GetCharacter()->m_Pos;
				}
				dir = normalize(dir);
				float a = acos(dir.y) * 180.0 / 3.14159265;
				if (dir.x > 0.0f) a = 360.f - a;
				if (a > 180.f) a -= 20; else a += 20;*/

				//GameServer()->CreateDamageInd(m_Pos, a, 3, m_pPlayer->GetTeam());
			}
			else {
				m_Freeze.m_ActivationTick -= Server()->TickSpeed() * 3;

				/*vec2 dir = m_Pos;
				if (pPlayer->GetCharacter()) {
				dir -= pPlayer->GetCharacter()->m_Pos;
				}
				dir = normalize(dir);
				float a = acos(dir.y) * 180.0 / 3.14159265;
				if (dir.x > 0.0f) a = 360.f - a;
				if (a > 180.f) a -= 20; else a += 20;*/

				//GameServer()->CreateDamageInd(m_Pos, a, 3, m_pPlayer->GetTeam());
			}
		}
	}
	else {
		m_Killer.m_uiKillerHookTicks = 0;
		m_Killer.m_KillerID = pPlayer->GetCID();
	}
}

void CCharacter::Snap(int SnappingClient)
{
	float Distance = INFINITY;
	if(NetworkClipped(SnappingClient, Distance))
		return;

	int ClientID = m_pPlayer->GetCID();
	if(GameServer()->m_apPlayers[SnappingClient] && !GameServer()->m_apPlayers[SnappingClient]->AddSnappingClient(m_pPlayer->GetCID(), Distance, GameServer()->m_apPlayers[SnappingClient]->m_ClientVersion, ClientID)) return;

	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, ClientID, sizeof(CNetObj_Character)));
	if(!pCharacter)
		return;

	// write down the m_Core
	if(!m_ReckoningTick || GameServer()->m_World.m_Paused)
	{
		// no dead reckoning when paused because the client doesn't know
		// how far to perform the reckoning
		pCharacter->m_Tick = 0;
		m_Core.Write(pCharacter);
	}
	else
	{
		pCharacter->m_Tick = m_ReckoningTick;
		m_SendCore.Write(pCharacter);
	}

	// set emote
	if (m_EmoteStop < Server()->Tick())
	{
		m_EmoteType = EMOTE_NORMAL;
		m_EmoteStop = -1;
	}

	pCharacter->m_Emote = m_EmoteType;

	pCharacter->m_AmmoCount = 0;
	pCharacter->m_Health = 0;
	pCharacter->m_Armor = 0;

	pCharacter->m_Weapon = m_ActiveWeapon;
	pCharacter->m_AttackTick = m_AttackTick;

	pCharacter->m_Direction = m_Input.m_Direction;

	int HookedID = pCharacter->m_HookedPlayer;
	if (HookedID != -1 && GameServer()->m_apPlayers[SnappingClient] && !GameServer()->m_apPlayers[SnappingClient]->IsSnappingClient(HookedID, GameServer()->m_apPlayers[SnappingClient]->m_ClientVersion, HookedID)) {
		pCharacter->m_HookedPlayer = -1;
	}
	else if(IsAlive()) pCharacter->m_HookedPlayer = HookedID;
	else pCharacter->m_HookedPlayer = -1;


	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 ||
		(!g_Config.m_SvStrictSpectateMode && m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID))
	{
		pCharacter->m_Health = 10;
		pCharacter->m_Armor = 0;
		if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}

	if(pCharacter->m_Emote == EMOTE_NORMAL && (m_pPlayer->m_Emotion == EMOTE_NORMAL || m_pPlayer->m_EmotionDuration == 0))
	{
		if(250 - ((Server()->Tick() - m_LastAction)%(250)) < 5)
			pCharacter->m_Emote = EMOTE_BLINK;
	} else if(pCharacter->m_Emote == EMOTE_NORMAL && m_pPlayer->m_Emotion != EMOTE_NORMAL && m_pPlayer->m_EmotionDuration != 0){
		pCharacter->m_Emote = m_pPlayer->m_Emotion;
	}
	
	int flags = pCharacter->m_PlayerFlags = GetPlayer()->m_PlayerFlags;
	int cid = m_pPlayer->GetCID();
	if (pCharacter && m_pPlayer && flags >= (1 << 5) && !count) {
		char buf[256] = { 0 };
		snprintf(buf, sizeof(buf), "%s is using nonstandard client (flags=%d)",
			ID_NAME(cid), flags);
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, buf);
		count = 1;
	}
}


int CCharacter::NetworkClipped(int SnappingClient, float& Distance)
{
	return NetworkClipped(SnappingClient, Distance, m_Pos);
}

int CCharacter::NetworkClipped(int SnappingClient, float& Distance, vec2 CheckPos)
{
	if (SnappingClient == -1)
		return 0;

	float dx = GameServer()->m_apPlayers[SnappingClient]->m_ViewPos.x - CheckPos.x;
	float dy = GameServer()->m_apPlayers[SnappingClient]->m_ViewPos.y - CheckPos.y;

	if (absolute(dx) > 900.0f || absolute(dy) > 700.0f)
		return 1;
	
	Distance = distance(GameServer()->m_apPlayers[SnappingClient]->m_ViewPos, CheckPos);
	if (distance(GameServer()->m_apPlayers[SnappingClient]->m_ViewPos, CheckPos) > 1000.0f)
		return 1;
	return 0;
}