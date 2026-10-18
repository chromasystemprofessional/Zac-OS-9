#include "expr.h"

#include <cmath>
#include <QList>
#include <functional>

struct ExprNode {
	std::function<double(double)> eval;
};

namespace {

using Node = std::shared_ptr<ExprNode>;

Node leaf(std::function<double(double)> f) {
	auto n = std::make_shared<ExprNode>();
	n->eval = std::move(f);
	return n;
}

class Parser {
public:
	explicit Parser(const QString &t) : s(t.simplified().remove(' ').toLower()) {}
	QString error;

	Node parse() {
		Node n = sum();
		if (n && i < s.size() && error.isEmpty()) {
			error = QString("“%1” isn't understood there.").arg(s.mid(i, 1));
		}
		return error.isEmpty() ? n : nullptr;
	}

private:
	QString s;
	int i = 0;

	bool eat(QChar c) {
		if (i < s.size() && s[i] == c) {
			i++;
			return true;
		}
		return false;
	}

	Node sum() {
		Node a = product();
		while (a && i < s.size() && (s[i] == '+' || s[i] == '-')) {
			const bool plus = s[i++] == '+';
			Node b = product();
			if (!b) return nullptr;
			a = plus ? leaf([a, b](double x) { return a->eval(x) + b->eval(x); })
				: leaf([a, b](double x) { return a->eval(x) - b->eval(x); });
		}
		return a;
	}

	/* Implied multiplication: a factor followed by something that starts one. */
	bool startsFactor() const {
		return i < s.size() && (s[i].isLetter() || s[i] == '(' || s[i].isDigit() || s[i] == '.');
	}

	Node product() {
		Node a = unary();
		while (a && i < s.size()) {
			if (s[i] == '*' || s[i] == '/') {
				const bool times = s[i++] == '*';
				Node b = unary();
				if (!b) return nullptr;
				a = times ? leaf([a, b](double x) { return a->eval(x) * b->eval(x); })
					: leaf([a, b](double x) { return a->eval(x) / b->eval(x); });
			} else if (startsFactor()) {
				Node b = power();
				if (!b) return nullptr;
				a = leaf([a, b](double x) { return a->eval(x) * b->eval(x); });
			} else {
				break;
			}
		}
		return a;
	}

	Node unary() {
		if (eat('-')) {
			Node a = unary();
			return a ? leaf([a](double x) { return -a->eval(x); }) : nullptr;
		}
		eat('+');
		return power();
	}

	Node power() {
		Node a = atom();
		if (a && eat('^')) {
			Node b = unary(); /* right to left: 2^3^2 is 2^9 */
			if (!b) return nullptr;
			return leaf([a, b](double x) { return std::pow(a->eval(x), b->eval(x)); });
		}
		return a;
	}

	Node atom() {
		if (i >= s.size()) {
			error = "Something is missing at the end.";
			return nullptr;
		}
		if (eat('(')) {
			Node a = sum();
			if (a && !eat(')')) {
				error = "A “)” is missing.";
				return nullptr;
			}
			return a;
		}
		if (s[i].isDigit() || s[i] == '.') {
			const int start = i;
			while (i < s.size() && (s[i].isDigit() || s[i] == '.')) i++;
			bool ok = false;
			const double v = s.mid(start, i - start).toDouble(&ok);
			if (!ok) {
				error = QString("“%1” isn't a number.").arg(s.mid(start, i - start));
				return nullptr;
			}
			return leaf([v](double) { return v; });
		}
		if (s[i].isLetter()) {
			static const QList<std::pair<QString, double (*)(double)>> functions = {
				{ "asin", [](double v) { return std::asin(v); } }, { "acos", [](double v) { return std::acos(v); } },
				{ "atan", [](double v) { return std::atan(v); } }, { "sqrt", [](double v) { return std::sqrt(v); } },
				{ "floor", [](double v) { return std::floor(v); } }, { "sin", [](double v) { return std::sin(v); } },
				{ "cos", [](double v) { return std::cos(v); } }, { "tan", [](double v) { return std::tan(v); } },
				{ "abs", [](double v) { return std::fabs(v); } }, { "exp", [](double v) { return std::exp(v); } },
				{ "log", [](double v) { return std::log10(v); } }, { "ln", [](double v) { return std::log(v); } },
			};
			for (const auto &[name, f] : functions) {
				if (s.mid(i, name.size()) == name) {
					i += name.size();
					Node a; /* "sin(x)" or "sin x" */
					if (eat('(')) {
						a = sum();
						if (a && !eat(')')) {
							error = "A “)” is missing.";
							return nullptr;
						}
					} else {
						a = power();
					}
					if (!a) return nullptr;
					auto fn = f;
					return leaf([a, fn](double x) { return fn(a->eval(x)); });
				}
			}
			if (s.mid(i, 2) == "pi") {
				i += 2;
				return leaf([](double) { return M_PI; });
			}
			if (eat('x')) return leaf([](double x) { return x; });
			if (eat('e')) return leaf([](double) { return M_E; });
		}
		error = QString("“%1” isn't understood.").arg(s.mid(i, 1));
		return nullptr;
	}
};

} // namespace

bool Expression::parse(const QString &text, QString *error) {
	Parser p(text);
	m_root = p.parse();
	if (error) {
		*error = p.error;
	}
	return bool(m_root);
}

double Expression::operator()(double x) const {
	return m_root ? m_root->eval(x) : NAN;
}
