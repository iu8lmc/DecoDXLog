#include "core/NewQsoFields.h"
#include <QHash>
#include <QSet>
#include <QVector>
#include <cmath>
#include <functional>
#include <optional>

namespace decolog::core::newqso {
namespace {
struct Instruction { QChar op; double literal{0}; QString reference; };
struct Program { QVector<Instruction> code; QStringList refs; };
class Parser {
public:
    explicit Parser(QString text) : m_text(std::move(text)) {}
    std::optional<Program> parse() {
        if (m_text.isEmpty() || m_text.size()>256 || !expression()) return {};
        whitespace(); if (m_pos!=m_text.size() || m_program.code.size()>128) return {};
        return m_program;
    }
private:
    void whitespace() { while (m_pos<m_text.size() && m_text[m_pos].isSpace()) ++m_pos; }
    bool take(QChar c) { whitespace(); if (m_pos<m_text.size() && m_text[m_pos]==c) { ++m_pos; return true; } return false; }
    bool expression() {
        if (!term()) return false;
        for (;;) { if (take('+')) { if (!term()) return false; m_program.code.append({'+',0,{}}); }
                   else if (take('-')) { if (!term()) return false; m_program.code.append({'-',0,{}}); } else return true; }
    }
    bool term() {
        if (!unary()) return false;
        for (;;) { if (take('*')) { if (!unary()) return false; m_program.code.append({'*',0,{}}); }
                   else if (take('/')) { if (!unary()) return false; m_program.code.append({'/',0,{}}); } else return true; }
    }
    bool unary() {
        if (++m_depth>32) { --m_depth; return false; }
        bool ok=false;
        if (take('+')) ok=unary();
        else if (take('-')) { ok=unary(); if (ok) m_program.code.append({'~',0,{}}); }
        else ok=primary();
        --m_depth; return ok;
    }
    bool primary() {
        if (take('(')) return expression() && take(')');
        if (take('{')) {
            const qsizetype end=m_text.indexOf('}',m_pos); if (end<0) return false;
            const QString name=normalizeAdifName(m_text.mid(m_pos,end-m_pos)); m_pos=end+1;
            if (!isValidAdifName(name)) return false;
            m_program.code.append({'r',0,name}); if (!m_program.refs.contains(name)) m_program.refs << name; return true;
        }
        whitespace(); const qsizetype start=m_pos; bool digit=false, dot=false;
        while (m_pos<m_text.size()) {
            const QChar c=m_text[m_pos];
            if (c>=QLatin1Char('0') && c<=QLatin1Char('9')) { digit=true; ++m_pos; }
            else if (c==QLatin1Char('.') && !dot) { dot=true; ++m_pos; }
            else break;
        }
        if (!digit) return false;
        bool ok=false; const double value=QLocale::c().toDouble(m_text.mid(start,m_pos-start),&ok);
        if (!ok || !std::isfinite(value)) return false; m_program.code.append({'n',value,{}}); return true;
    }
    QString m_text; qsizetype m_pos{0}; int m_depth{0}; Program m_program;
};
struct Compiled { QHash<QString,QVariantMap> definitions; QHash<QString,Program> programs; QString error, field; };
Compiled compileDefinitions(const QVariantList& definitions)
{
    Compiled out;
    for (const QVariant& value : definitions) { const auto item=value.toMap(); out.definitions.insert(item.value(QStringLiteral("name")).toString(),item); }
    for (const QVariant& value : definitions) {
        const auto item=value.toMap(); const QString name=item.value(QStringLiteral("name")).toString(), formula=item.value(QStringLiteral("formula")).toString();
        if (formula.isEmpty()) continue;
        const auto program=Parser(formula).parse();
        if (!program || item.value(QStringLiteral("type")).toString()!=QLatin1String("number")) { out.field=name; out.error=QStringLiteral("Invalid formula syntax"); return out; }
        for (const QString& ref : program->refs) {
            if (!out.definitions.contains(ref) || out.definitions.value(ref).value(QStringLiteral("type")).toString()!=QLatin1String("number")) {
                out.field=name; out.error=QStringLiteral("Formula references an unknown or non-numeric field"); return out;
            }
        }
        out.programs.insert(name,*program);
    }
    QHash<QString,int> state;
    std::function<bool(const QString&)> visit=[&](const QString& name) {
        if (state.value(name)==1) { out.field=name; out.error=QStringLiteral("Cyclic formula dependency"); return false; }
        if (state.value(name)==2) return true; state[name]=1;
        for (const QString& ref : out.programs.value(name).refs) if (out.programs.contains(ref) && !visit(ref)) return false;
        state[name]=2; return true;
    };
    for (const QVariant& value : definitions) {
        const QString name=value.toMap().value(QStringLiteral("name")).toString();
        if (out.programs.contains(name) && !visit(name)) break;
    }
    return out;
}
}
QString validateDerivedFormulas(const QVariantList& definitions,QString* invalidField)
{
    const auto compiled=compileDefinitions(definitions); if (invalidField) *invalidField=compiled.field; return compiled.error;
}
DerivedValuesResult computeDerivedValues(const QVariantList& definitions,const QVariantMap& values,const QLocale& locale)
{
    DerivedValuesResult out; const auto checked=validateCustomFields(definitions);
    if (!checked.ok()) { out.error=QStringLiteral("Invalid custom field definition: %1").arg(checked.field); return out; }
    const auto compiled=compileDefinitions(checked.fields);
    if (!compiled.error.isEmpty()) { out.error=compiled.field+QStringLiteral(": ")+compiled.error; return out; }
    QVariantMap normalized;
    for (auto it=values.cbegin();it!=values.cend();++it) {
        const QString name=normalizeAdifName(it.key()); if (normalized.contains(name)) { out.error=QStringLiteral("%1: Duplicate field").arg(name); return out; }
        normalized.insert(name,it.value().toString().trimmed());
    }
    for (const QVariant& value : checked.fields) {
        const QString name=value.toMap().value(QStringLiteral("name")).toString();
        if (compiled.programs.contains(name) && !normalized.value(name).toString().isEmpty()) { out.error=QStringLiteral("%1: Derived value cannot be supplied manually").arg(name); return out; }
    }
    using Number=std::optional<double>; QHash<QString,Number> cache;
    std::function<Number(const QString&)> evalField;
    evalField=[&](const QString& name)->Number {
        if (!out.error.isEmpty()) return {};
        if (cache.contains(name)) return cache.value(name);
        if (!compiled.programs.contains(name)) {
            const QString text=normalized.value(name).toString(); if (text.isEmpty()) { cache.insert(name,{}); return {}; }
            QLocale strict=locale; strict.setNumberOptions(strict.numberOptions()|QLocale::RejectGroupSeparator);
            bool ok=false; const double n=strict.toDouble(text,&ok);
            if (!ok || !std::isfinite(n)) { out.error=QStringLiteral("%1: Invalid numeric input").arg(name); return {}; }
            cache.insert(name,n); return n;
        }
        QVector<Number> stack;
        for (const Instruction& instruction : compiled.programs.value(name).code) {
            if (instruction.op==QLatin1Char('n')) { stack.append(instruction.literal); continue; }
            if (instruction.op==QLatin1Char('r')) { stack.append(evalField(instruction.reference)); if (!out.error.isEmpty()) return {}; continue; }
            if (instruction.op==QLatin1Char('~')) { if (stack.isEmpty()) { out.error=QStringLiteral("Invalid formula"); return {}; } if (stack.last()) stack.last()=-*stack.last(); continue; }
            if (stack.size()<2) { out.error=QStringLiteral("Invalid formula"); return {}; }
            const Number rhs=stack.takeLast(),lhs=stack.takeLast();
            if (!lhs || !rhs) { stack.append(Number{}); continue; }
            if (instruction.op==QLatin1Char('/') && *rhs==0) { out.error=QStringLiteral("%1: Division by zero").arg(name); return {}; }
            const double result=instruction.op==QLatin1Char('+') ? *lhs+*rhs : instruction.op==QLatin1Char('-') ? *lhs-*rhs : instruction.op==QLatin1Char('*') ? (*lhs)*(*rhs) : *lhs / *rhs;
            if (!std::isfinite(result)) { out.error=QStringLiteral("%1: Non-finite formula result").arg(name); return {}; }
            stack.append(result);
        }
        if (stack.size()!=1) { out.error=QStringLiteral("Invalid formula"); return {}; }
        cache.insert(name,stack.first()); return stack.first();
    };
    for (const QVariant& value : checked.fields) {
        const QString name=value.toMap().value(QStringLiteral("name")).toString(); if (!compiled.programs.contains(name)) continue;
        const Number n=evalField(name); if (!out.error.isEmpty()) { out.values.clear(); return out; }
        if (n) out.values.insert(name,QString::number(*n,'g',15));
    }
    return out;
}
}
