#pragma once

/*
 * Plotter's expressions: numbers, x, pi, e; + - * / ^ (power, right to
 * left), unary minus, brackets, implied multiplication ("2x", "3(x+1)",
 * "2sin x"), and sin cos tan asin acos atan sqrt abs ln log exp floor.
 */

#include <QString>
#include <memory>

struct ExprNode;

class Expression {
public:
	/* False, with the reason, if `text` isn't one. */
	bool parse(const QString &text, QString *error = nullptr);
	/* NaN where it isn't defined (sqrt(-1), 1/0 gives inf). */
	double operator()(double x) const;
	bool valid() const { return bool(m_root); }

private:
	std::shared_ptr<ExprNode> m_root;
};
