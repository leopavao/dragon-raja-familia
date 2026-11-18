#pragma once

///////////////////////////////////////////////////////////////////////////////
// BEGIN : Singleton 생성 패턴을 위한 클래스
template<typename T>
class TSingleton
{
public:
	TSingleton()
	{
		_ASSERT(s_pInstance == NULL);
		s_pInstance = (T*)((int)this + 
			               (int)(T*)1 - (int)(TSingleton<T>*)(T*)1);
	}
	
	virtual ~TSingleton() 
	{ 
		_ASSERT(s_pInstance != NULL);
		s_pInstance = NULL; 
	}
	
public:
	static T& Reference() 
	{ 
		_ASSERT(s_pInstance != NULL);
		return *s_pInstance; 
	}
	
	static T* Pointer()   
	{ 
		_ASSERT(s_pInstance != NULL);
		return s_pInstance; 
	}

private:
	static T* s_pInstance;
};
// END
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// BEGIN
template<typename T> T* TSingleton<T>::s_pInstance = NULL;
// END
///////////////////////////////////////////////////////////////////////////////