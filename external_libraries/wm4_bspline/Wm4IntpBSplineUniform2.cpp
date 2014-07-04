// Wild Magic Source Code
// David Eberly
// http://www.geometrictools.com
// Copyright (c) 1998-2008
//
// This library is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or (at
// your option) any later version.  The license is available for reading at
// either of the locations:
//     http://www.gnu.org/copyleft/lgpl.html
//     http://www.geometrictools.com/License/WildMagicLicense.pdf
//
// Version: 4.0.0 (2006/06/28)

#include "Wm4FoundationPCH.h"
#include "Wm4IntpBSplineUniform2.h"
#include "Wm4Math.h"

namespace Wm4
{
//----------------------------------------------------------------------------
template <class Real>
IntpBSplineUniform2<Real>::IntpBSplineUniform2 (int iDegree, const int* aiDim,
    Real* afData)
    :
    IntpBSplineUniform<Real>(2,iDegree,aiDim,afData)
{
}
//----------------------------------------------------------------------------
template <class Real>
int IntpBSplineUniform2<Real>::Index (int iX, int iY) const
{
    return iX + m_aiDim[0]*iY;
}
//----------------------------------------------------------------------------
template <class Real>
Real IntpBSplineUniform2<Real>::operator() (Real* afX)
{
    return (*this)(afX[0],afX[1]);
}
//----------------------------------------------------------------------------
template <class Real>
Real IntpBSplineUniform2<Real>::operator() (int* aiDx, Real* afX)
{
    return (*this)(aiDx[0],aiDx[1],afX[0],afX[1]);
}
//----------------------------------------------------------------------------
template <class Real>
Real IntpBSplineUniform2<Real>::operator() (Real fX, Real fY)
{
    m_aiBase[0] = (int)Math<Real>::Floor(fX);
    m_aiBase[1] = (int)Math<Real>::Floor(fY);
    for (int iDim = 0; iDim < 2; iDim++)
    {
        if (m_aiOldBase[iDim] != m_aiBase[iDim])
        {
            // switch to new local grid
            for (int k = 0; k < 2; k++)
            {
                m_aiOldBase[k] = m_aiBase[k];
                m_aiGridMin[k] = m_aiBase[k] - 1;
                m_aiGridMax[k] = m_aiGridMin[k] + m_iDegree;
            }

            // fill in missing grid data if necessary
            if (m_oEvaluateCallback)
            {
                EvaluateUnknownData();
            }

            ComputeIntermediate();
            break;
        }
    }

    this->SetPolynomial(0,fX-m_aiBase[0],m_aafPoly[0]);
    this->SetPolynomial(0,fY-m_aiBase[1],m_aafPoly[1]);

    int aiI[2] = { 0, 0 };
    Real fResult = (Real)0.0;
    for (int k = aiI[0]+m_iDp1*aiI[1]; k < m_iDp1ToN; k++)
    {
        fResult += m_aafPoly[0][aiI[0]]*m_aafPoly[1][aiI[1]]*m_afInter[k];

        if (++aiI[0] <= m_iDegree)
        {
            continue;
        }
        aiI[0] = 0;

        aiI[1]++;
    }
    return fResult;
}
//----------------------------------------------------------------------------
template <class Real>
Real IntpBSplineUniform2<Real>::operator() (int iDx, int iDy, Real fX,
    Real fY)
{
    m_aiBase[0] = (int)Math<Real>::Floor(fX);
    m_aiBase[1] = (int)Math<Real>::Floor(fY);
    for (int iDim = 0; iDim < 2; iDim++)
    {
        if (m_aiOldBase[iDim] != m_aiBase[iDim])
        {
            // switch to new local grid
            for (int k = 0; k < 2; k++)
            {
                m_aiOldBase[k] = m_aiBase[k];
                m_aiGridMin[k] = m_aiBase[k] - 1;
                m_aiGridMax[k] = m_aiGridMin[k] + m_iDegree;
            }

            // fill in missing grid data if necessary
            if (m_oEvaluateCallback)
            {
                EvaluateUnknownData();
            }

            ComputeIntermediate();
            break;
        }
    }

    this->SetPolynomial(iDx,fX-m_aiBase[0],m_aafPoly[0]);
    this->SetPolynomial(iDy,fY-m_aiBase[1],m_aafPoly[1]);

    int aiI[2] = { iDx, iDy };
    int iIncr1 = iDx;
    Real fResult = (Real)0.0;
    for (int k = aiI[0]+m_iDp1*aiI[1]; k < m_iDp1ToN; k++)
    {
        fResult += m_aafPoly[0][aiI[0]]*m_aafPoly[1][aiI[1]]*m_afInter[k];

        if (++aiI[0] <= m_iDegree)
        {
            continue;
        }
        aiI[0] = iDx;
        k += iIncr1;

        aiI[1]++;
    }
    return fResult;
}
//----------------------------------------------------------------------------
template <class Real>
void IntpBSplineUniform2<Real>::EvaluateUnknownData ()
{
    for (int k1 = m_aiGridMin[1]; k1 <= m_aiGridMax[1]; k1++)
    {
        for (int k0 = m_aiGridMin[0]; k0 <= m_aiGridMax[0]; k0++)
        {
            int iIndex = Index(k0,k1);
            if (m_afData[iIndex] == Math<Real>::MAX_REAL)
            {
                m_afData[iIndex] = m_oEvaluateCallback(iIndex);
            }
        }
    }
}
//----------------------------------------------------------------------------
template <class Real>
void IntpBSplineUniform2<Real>::ComputeIntermediate ()
{
    // fetch subblock of data to cache
    int iDelta0 = m_aiDim[0] - m_iDp1;
    int aiLoop[2];
    for (int iDim = 0; iDim < 2; iDim++)
    {
        aiLoop[iDim] = m_aiGridMin[iDim];
    }
    int iIndex = Index(aiLoop[0],aiLoop[1]);
    int k;
    for (k = 0; k < m_iDp1ToN; k++, iIndex++)
    {
        m_afCache[k] = m_afData[iIndex];

        if (++aiLoop[0] <= m_aiGridMax[0])
        {
            continue;
        }
        aiLoop[0] = m_aiGridMin[0];
        iIndex += iDelta0;

        aiLoop[1]++;
    }

    // compute and save the intermediate product
    for (int i = 0, j = 0; i < m_iDp1ToN; i++)
    {
        Real fSum = (Real)0.0;
        for (k = 0; k < m_iDp1ToN; k += m_aiSkip[j], j += m_aiSkip[j])
        {
            fSum += m_afProduct[j]*m_afCache[k];
        }
        m_afInter[i] = fSum;
    }
}
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// explicit instantiation
//----------------------------------------------------------------------------
template WM4_FOUNDATION_ITEM
class IntpBSplineUniform2<float>;

template WM4_FOUNDATION_ITEM
class IntpBSplineUniform2<double>;
//----------------------------------------------------------------------------
}
