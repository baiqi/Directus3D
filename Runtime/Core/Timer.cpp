/*
Copyright(c) 2016-2018 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES =====
#include "Timer.h"
//================

//= NAMESPACES ========
using namespace std;
using namespace chrono;
//=====================

namespace Directus
{
	Timer::Timer(Context* context) : Subsystem(context)
	{
		m_deltaTimeSec	= 0.0f;
		m_deltaTimeMs	= 0.0f;
		m_firstRun		= true;
	}

	Timer::~Timer()
	{

	}

	void Timer::Tick()
	{
		auto currentTime = high_resolution_clock::now();
		duration<double, milli> ms = currentTime - m_previousTime;
		m_previousTime = currentTime;

		m_deltaTimeMs	= (float)ms.count();
		m_deltaTimeSec	= (float)(ms.count() / 1000.0f);

		if (m_firstRun)
		{
			m_deltaTimeMs = 0.0f;
			m_deltaTimeSec = 0.0f;
			m_firstRun = false;
		}
	}
}
