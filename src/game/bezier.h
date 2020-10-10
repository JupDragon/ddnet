#ifndef GAME_BEZIER_H
#define GAME_BEZIER_H

// Evaluates the Bernstein polynomial of degree 3/a one-dimensional Bezier curve
//
// https://en.wikipedia.org/w/index.php?title=Bernstein_polynomial&oldid=965314973
//
// f(t) = (1-t)³ a + 3(1-t)²t b + 3(1-t)t² c + t³ d
class CCubicBezier
{
	float m_A;
	float m_B;
	float m_C;
	float m_D;
	CCubicBezier(float a, float b, float c, float d)
	{
		m_A = a;
		m_B = b;
		m_C = c;
		m_D = d;
	}

public:
	CCubicBezier() {}
	float Evaluate(float t) const;
	float Derivative(float t) const;
	static CCubicBezier With(float Start, float StartDerivative, float EndDerivative, float End);
};

#endif // GAME_BEZIER_H
