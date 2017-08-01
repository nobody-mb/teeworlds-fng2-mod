/*
    BASIC ANTI-CHEATS SYSTEM FOR TEEWORLDS (SERVER SIDE)
    By unsigned char*
*/
#ifndef H_CHARACTER_ANTICHEATS
#define H_CHARACTER_ANTICHEATS
#include <game/generated/protocol.h>

#define MAX_CONTROL_INPUTS  16

namespace twac
{

	class CCharacter
	{
	public:
		enum
		{
			FIREBOT = 1,
			SPINBOT,
			AIMBOT
		};

		CCharacter();

		int CheckInputs(int ServerTick, CNetObj_PlayerInput LatestInput, CNetObj_PlayerInput LatestPrevInput);

		bool m_UseAntiCheats;
		bool m_UseAntiFireBot;
		bool m_UseAntiSpinBot;
		bool m_UseAntiAimBot;

	protected:
		struct CControlInputRegistry
		{
			float m_Cross;
			int m_Fire;
			int m_Tick;
		} m_aControlInput[MAX_CONTROL_INPUTS];

		int m_ControlInputCount;
		int m_LastActionTickSpinBot;
		int m_ControlStatusAimBot;
		int m_LastNoShootTimeAimBot;
		int m_ControlStatusFireBot;

	private:
		void Reset();
	};

}

#endif // H_CHARACTER_ANTICHEATS
