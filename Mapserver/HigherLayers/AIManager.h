// AIManager.h: interface for the CAIManager class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_AIMANAGER_H__35FD94E6_6CDA_44B0_B856_4555FCD844C6__INCLUDED_)
#define AFX_AIMANAGER_H__35FD94E6_6CDA_44B0_B856_4555FCD844C6__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

///////////////////////////////////////////////////////////////////////////////
// NPC AI 관리를 위한 클래스
class CAIManager : public TSingleton<CAIManager> 
{
public:
	CAIManager();
	virtual ~CAIManager();

public :
	int					Load_AI_Data();
	int					LoadMagicBag();
	void				Clear_AI_Data();
	void				ClearMagicBag();
	int					LoadBossData();
	int					LoadAttackBag();
	int					LoadActionBag();
	void				ClearBossData();
	void				ClearAttackBag();
	void				ClearActionBag();


	int**		GetNpcAIData();
	int*			GetNpcAIData(int AI_Type);
	int			GetNpcAIData(int AI_Type,int Field);

	void			SetAI(CHARLIST* Npc,int AIPattern);
	void			SetTarget(CHARLIST* Npc,int Target);
	void			SendBossAction(CHARLIST* Npc,int AttackMethod);

	CHARLIST*			GetDragonPtr(CHARLIST* Npc);							// LTS DRAGON MODIFY
	void				CheckDeath(CHARLIST* Defender);							// LTS DRAGON MODIFY

	void				ProcessAIBAttacked(CHARLIST* Npc);
	void				ProcessNPCAI(CHARLIST* Npc);

private:
	int			GetMagicBagData(int Row,int Col);

	bool	isProcess(CHARLIST* Npc);

	int			CheckAIDelayTime(CHARLIST* Npc);
	void			SetAIDelayTime(CHARLIST* Npc,DWORD time);
	void			SetAIDelayTime(CHARLIST* Npc,int Status,int StatusSub);

	int			GetNPCSearchRange(CHARLIST* Npc);

	void			SetNextAI(CHARLIST* Npc);
	void			SetAIDefault(CHARLIST* Npc);
	void			SetAIBDefault(CHARLIST* Npc);

	void			SetAIMove(CHARLIST* Npc);
	void			SetAIBMove(CHARLIST* Npc);

	void			SelectAttackMethod(CHARLIST* Npc);
	void			SelectBAttackMethod(CHARLIST* Npc);
	void			ClearAttackMethod(CHARLIST* Npc);
	int			AttackableRange(CHARLIST* Npc);
	int			GetAttackRange(CHARLIST* Npc);
	int			GetMagicAttackRange(CHARLIST* Npc,int MethodField);
	int			GetPhysicalAttackRange(CHARLIST* Npc);
	int			GetSpecialAttackRange(CHARLIST* Npc,int Method);
	int			CheckAttackMethod(CHARLIST* Npc,int MethodField);
	int			CheckBAttackMethod(CHARLIST* Npc);
	int			GetAttackChange(CHARLIST* Npc);
	int			CheckSummon(CHARLIST* Npc);
	
	CHARLIST*	GetTarget(int TargetID);
	int			FindTarget(CHARLIST* Npc);
	void			FindTarget(CHARLIST* Npc,int& Target);
	void			ClearTarget(CHARLIST* Npc);
	
	void			RandomMove(CHARLIST* Npc);
	void			TargetMove(CHARLIST* Npc);
	void			ClearMovePattern(CHARLIST* Npc);

	int			CheckAndDoAttack(CHARLIST* Npc);
	void			SendAttack(CHARLIST* Npc);
	void			SendBAttack(CHARLIST* Npc);
	void			SendMagicAttack(CHARLIST* Npc,int Field);
	void			SendMagicBAttack(CHARLIST* Npc);

	void			SendPhysicalAttack(CHARLIST* Npc);
	void			CheckAndSendAttack(CHARLIST* Npc,int MethodField);
	void			CheckAndSendBAttack(CHARLIST* Npc);
	void			SelectSummonNPC(CHARLIST* Npc);
	void			SendSummon(CHARLIST* Npc,int NpcIndex);
	void			SendNPCAction(CHARLIST* Npc,int Action);
	void			SendBossAction(CHARLIST* Npc);

	void				CheckMoveWander(CHARLIST* Npc);
	void				CheckMoveAccess(CHARLIST* Npc);

	void			SetRunStatus(CHARLIST* Npc);
	void			SetRecoveryStatus(CHARLIST* Npc);
	void			CheckStatusUp(CHARLIST* Npc);

	int**		GetBossStatusData();
	int*			GetBossStatusData(int TypeNo);
	int			GetBossStatusData(int TypeNo,int Field);
	int			GetAttackBagNo(CHARLIST* Npc);
	int			GetAttackMethodFromAttackBag(int AttackBagNo);

	int			CheckHPGrade(CHARLIST* Npc);
	void			CheckHPStatus(CHARLIST* Npc);
	int			inHPRange(CHARLIST* Npc,int sField,int eField,int Percent);
	void			OnHPAction(CHARLIST* Npc,int HPGrade);
	void			OnCloseHPAction(CHARLIST* Npc);
	void			SetActionFunc(CHARLIST* Npc,int HPGrade,int* Action);
	void			CallActionFunc(CHARLIST* Npc,int* Action);
	void			CheckAndModifyTarget(CHARLIST* Npc);

	CHARLIST*	GetNearNPC(CHARLIST* Npc,int IndexNo,int Range);		// LTS DRAGON MODIFY
	int			CheckDragonPtr(CHARLIST* Npc);							// LTS DRAGON MODIFY

	void			CheckSummonDeath(CHARLIST* Defender);						// LTS DRAGON MODIFY
	void			CheckDragonHeartDeath(CHARLIST* Defenderc);					// LTS DRAGON MODIFY

	void			ProcessAIWander(CHARLIST* Npc);
	void			ProcessAIAccess(CHARLIST* Npc);
	void			ProcessAIAttack(CHARLIST* Npc);
	void			ProcessAIAttacked(CHARLIST* Npc);
	void			ProcessAIMoving(CHARLIST* Npc);

	void			ProcessAIBWander(CHARLIST* Npc);
	void			ProcessAIBAccess(CHARLIST* Npc);
	void			ProcessAIBAttack(CHARLIST* Npc);
	void			ProcessAIBMoving(CHARLIST* Npc);
	void			ProcessAIBBackDraw(CHARLIST* Npc);
	void			ProcessAIBWait(CHARLIST* Npc);
	void			ProcessAIBAvoid(CHARLIST* Npc);

private:
	int**				m_pNpc_AI_Data;
	int					m_iAI_DataCount;
	int**				m_pMagicBag;
	int					m_iMagicBagCount;

	int**				m_pBossStatusData;
	int					m_iBossStatusCount;
	int**				m_pAttackBag;
	int					m_iAttackBagCount;
	int**				m_pActionBag;
	int					m_iActionBagCount;

	CHARLIST*			m_pDragon;							// LTS DRAGON MODIFY
};
//
///////////////////////////////////////////////////////////////////////////////
#endif // !defined(AFX_AIMANAGER_H__35FD94E6_6CDA_44B0_B856_4555FCD844C6__INCLUDED_)
