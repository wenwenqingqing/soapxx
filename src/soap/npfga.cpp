#include "soap/base/tokenizer.hpp"
#include "soap/base/exceptions.hpp"
#include "soap/npfga.hpp"
#include "soap/globals.hpp"
#include "soap/linalg/numpy.hpp"
#include "boost/format.hpp"
#include <algorithm>

namespace soap { namespace npfga {

// ==============
// FNodeDimension
// ==============

FNodeDimension::FNodeDimension(std::string dimstr) {
    std::vector<std::string> fields = soap::base::Tokenizer(dimstr, "*").ToVector();
    for (auto it=fields.begin(); it!=fields.end(); ++it) {
        std::vector<std::string> unit_power = soap::base::Tokenizer(*it, "^").ToVector();
        std::string unit = unit_power[0];
        double power = (unit_power.size() > 1) ? 
            soap::lexical_cast<double, std::string>(unit_power[1], "invalid power") : 1.0;
        this->addFactor(unit, power);
    }
}

std::string FNodeDimension::calculateString() {
    std::string out = "";
    for (auto it=dim_map.begin(); it!=dim_map.end(); ++it) {
        out += "*" + it->first + "^" + 
            soap::lexical_cast<std::string, double>(it->second, "unit error");
    }
    return out;
}

void FNodeDimension::eraseZeros() {
    for (auto it=dim_map.begin(); it!=dim_map.end(); ) { // NOTE No increment!
        if (std::abs(it->second) < 1e-5) it = dim_map.erase(it);
        else if (it->first == "") it = dim_map.erase(it);
        else ++it;
    }
}

void FNodeDimension::raiseToPower(double p) {
    for (auto it=dim_map.begin(); it!=dim_map.end(); ++it) {
        it->second *= p;
    }
    this->eraseZeros();
}

void FNodeDimension::add(FNodeDimension &other) {
    for (auto it=other.dim_map.begin(); it!=other.dim_map.end(); ++it)
        this->addFactor(it->first, it->second);
    this->eraseZeros();
}

void FNodeDimension::subtract(FNodeDimension &other) {
    for (auto it = other.dim_map.begin(); it!=other.dim_map.end(); ++it)
        this->subtractFactor(it->first, it->second);
    this->eraseZeros();
}

void FNodeDimension::addFactor(const std::string &unit, const double &power) {
    auto it = dim_map.find(unit);
    if (it == dim_map.end()) dim_map[unit] = power;
    else dim_map[unit] += power;
}

void FNodeDimension::subtractFactor(const std::string &unit, const double &power) {
    auto it = dim_map.find(unit);
    if (it == dim_map.end()) dim_map[unit] = - power;
    else dim_map[unit] -= power;
}

bool FNodeDimension::matches(FNodeDimension &other, bool check_reverse) {
    bool matches = true;
    for (auto it=dim_map.begin(); it!=dim_map.end(); ++it) {
        if (other.dim_map.find(it->first) == other.dim_map.end()) {
            matches = false;
            break;
        } else if (std::abs(other.dim_map[it->first]-it->second) > 1e-5) {
            matches = false;
            break;
        }
    }
    if (check_reverse) return matches && other.matches(*this, false);
    else return matches;
}

// =========
// Operators
// =========

FNode *Operator::generateAndCheck(FNode *f1, FNodeCheck &chk) {
    FNode *new_node = NULL;
    if (this->checkInput(f1)) {
        new_node = this->generate(f1);
        if (chk.check(new_node)) {
            ;
        } else {
            delete new_node;
            new_node = NULL;
        }
    }
    return new_node;
}

FNode *Operator::generateAndCheck(FNode *f1, FNode *f2, FNodeCheck &chk) {
    FNode *new_node = NULL;
    if (this->checkInput(f1, f2)) {
        new_node = this->generate(f1, f2);
        if (!chk.check(new_node)) {
            delete new_node;
            new_node = NULL;
        }
    }
    return new_node;
}

std::string OExp::format(std::vector<std::string> &argstr) {
    return (boost::format("exp(%1$s)") % argstr[0]).str();
}

std::string OLog::format(std::vector<std::string> &argstr) {
    return (boost::format("log(%1$s)") % argstr[0]).str();
}

std::string OMod::format(std::vector<std::string> &argstr) {
    return (boost::format("|%1$s|") % argstr[0]).str();
}

std::string OPlus::format(std::vector<std::string> &argstr) {
    std::string expr = "";
    for (int i=0; i<argstr.size(); ++i) {
        expr += argstr[i];
        if (i<argstr.size()-1) expr += "+";
    }
    return expr;
}
std::string OMult::format(std::vector<std::string> &argstr) {
    std::string expr = "";
    for (int i=0; i<argstr.size(); ++i) {
        expr += argstr[i];
        if (i<argstr.size()-1) expr += "*";
    }
    return expr;
}


bool OExp::checkInput(FNode *f1) {
    return f1->isDimensionless() 
        && !(f1->containsOperator("e"))
        && (f1->getOperator()->getTag() != "l");
}

bool OLog::checkInput(FNode *f1) {
    return f1->isDimensionless() 
        && f1->notNegative() 
        && f1->notZero()
        && (f1->getOperator()->getTag() != "e")
        && !(f1->containsOperator("l"));
}

bool OMod::checkInput(FNode *f1) {
    return !(f1->notNegative());
}

bool OSqrt::checkInput(FNode *f1) {
    return f1->notNegative() 
        && (f1->getOperator()->getTag() != "2");
}

bool OInv::checkInput(FNode *f1) {
    return f1->notZero() 
        && (f1->getOperator()->getTag() != "r");
}

bool O2::checkInput(FNode *f1) {
    return true;
}

bool OPlus::checkInput(FNode *f1, FNode *f2) {
    return f1->getDimension().matches(f2->getDimension());
}

bool OMinus::checkInput(FNode *f1, FNode *f2) {
    return f1->getDimension().matches(f2->getDimension());
}

bool OMult::checkInput(FNode *f1, FNode *f2) {
    return true;
}

bool ODiv::checkInput(FNode *f1, FNode *f2) {
    return f2->notZero();
}

FNode *OExp::generate(FNode *f1) {
    bool maybe_neg = false;
    bool maybe_zero = false;
    FNode *new_node = new FNode(this, f1, maybe_neg, maybe_zero);
    return new_node;
}

FNode *OLog::generate(FNode *f1) {
    bool maybe_neg = true;
    bool maybe_zero = true;
    FNode *new_node = new FNode(this, f1, maybe_neg, maybe_zero);
    return new_node;
}

FNode *OMod::generate(FNode *f1) {
    bool maybe_neg = false;
    bool maybe_zero = !(f1->notZero());
    FNode *new_node = new FNode(this, f1, maybe_neg, maybe_zero);
    new_node->getDimension().add(f1->getDimension());
    return new_node;
}

FNode *OSqrt::generate(FNode *f1) {
    bool maybe_neg = false;
    bool maybe_zero = !(f1->notZero());
    FNode *new_node = new FNode(this, f1, maybe_neg, maybe_zero);
    new_node->getDimension().add(f1->getDimension());
    new_node->getDimension().raiseToPower(0.5);
    return new_node;
}

FNode *OInv::generate(FNode *f1) {
    bool maybe_neg = !(f1->notNegative());
    bool maybe_zero = false;
    FNode *new_node = new FNode(this, f1, maybe_neg, maybe_zero);
    new_node->getDimension().add(f1->getDimension());
    new_node->getDimension().raiseToPower(-1);
    return new_node;
}

FNode *O2::generate(FNode *f1) {
    bool maybe_neg = false;
    bool maybe_zero = !(f1->notZero());
    FNode *new_node = new FNode(this, f1, maybe_neg, maybe_zero);
    new_node->getDimension().add(f1->getDimension());
    new_node->getDimension().raiseToPower(2);
    return new_node;
}

FNode *OPlus::generate(FNode *f1, FNode *f2) {
    bool maybe_neg = !(f1->notNegative() && f2->notNegative());
    bool maybe_zero = !(!maybe_neg && (f1->notZero() || f2->notZero()));
    FNode *new_node = new FNode(this, f1, f2, maybe_neg, maybe_zero);
    new_node->getDimension().add(f1->getDimension());
    assert(new_node->getDimension().matches(f2->getDimension())); // TODO Make debug
    return new_node;
}

FNode *OMinus::generate(FNode *f1, FNode *f2) {
    bool maybe_neg = true;
    bool maybe_zero = true;
    FNode *new_node = new FNode(this, f1, f2, maybe_neg, maybe_zero);
    new_node->getDimension().add(f1->getDimension());
    assert(new_node->getDimension().matches(f2->getDimension())); // TODO Make debug
    return new_node;
}

FNode *OMult::generate(FNode *f1, FNode *f2) {
    bool maybe_neg = !(f1->notNegative() && f2->notNegative());
    bool maybe_zero = !(!maybe_neg && (f1->notZero() || f2->notZero()));
    FNode *new_node = new FNode(this, f1, f2, maybe_neg, maybe_zero);
    new_node->getDimension().add(f1->getDimension());
    new_node->getDimension().add(f2->getDimension());
    return new_node;
}

FNode *ODiv::generate(FNode *f1, FNode *f2) {
    bool maybe_neg = !(f1->notNegative() && f2->notNegative());
    bool maybe_zero = false;
    FNode *new_node = new FNode(this, f1, f2, maybe_neg, maybe_zero);
    new_node->getDimension().add(f1->getDimension());
    new_node->getDimension().subtract(f2->getDimension());
    return new_node;
}

OP_MAP *OP_MAP::instance = NULL;

Operator *OP_MAP::get(std::string tag) {
    OP_MAP *this_instance = OP_MAP::getInstance();
    return OP_MAP::getInstance()->op_map[tag];
}

OP_MAP *OP_MAP::getInstance() {
    if (OP_MAP::instance) ;
    else OP_MAP::instance = new OP_MAP();
    return OP_MAP::instance;
}

OP_MAP::~OP_MAP() {
    for (auto it=op_map.begin(); it!= op_map.end(); ++it)
        delete it->second;
    op_map.clear();
}

OP_MAP::OP_MAP() {
    op_map["I"] = new OIdent();
    op_map["*"] = new OMult();
    op_map[":"] = new ODiv();
    op_map["+"] = new OPlus();
    op_map["-"] = new OMinus();
    op_map["e"] = new OExp();
    op_map["l"] = new OLog();
    op_map["|"] = new OMod();
    op_map["s"] = new OSqrt();
    op_map["r"] = new OInv();
    op_map["2"] = new O2();
}

// =====
// FNode
// =====

FNode::FNode(Operator *oper, std::string varname, std::string maybe_neg,
        std::string maybe_zero, std::string dimstr, bool is_root) 
        : prefactor(1.0), value(0.0), instruction(NULL), 
          op(oper), tag(varname), is_root(is_root), generation_idx(0) {
    if (maybe_neg != "+-" && maybe_neg != "+") throw soap::base::OutOfRange(maybe_neg);
    if (maybe_zero != "+0" && maybe_zero != "-0") throw soap::base::OutOfRange(maybe_zero);
    maybe_negative = (maybe_neg == "+-") ? true : false;
    maybe_zero = (maybe_zero == "+0") ? true : false;
    dimension = FNodeDimension(dimstr);
    GLOG() << "Created " << (is_root ? "root" : "") << "node '" << varname << "': Units ";
    for (auto it=dimension.dim_map.begin(); it!=dimension.dim_map.end(); ++it)
        GLOG() << it->first << "^" << it->second << " ";
    GLOG() << std::endl;
}

FNode::FNode(Operator *oper, FNode *par1, FNode *par2, bool maybe_neg, bool maybe_z)
        : prefactor(1.0), value(0.0), instruction(NULL), 
          maybe_negative(maybe_neg), maybe_zero(maybe_z), 
          op(oper), is_root(false) {
    parents.push_back(par1);
    parents.push_back(par2);
    generation_idx = (par1->getGenerationIdx() >= par2->getGenerationIdx()) ? 
        par1->getGenerationIdx()+1 : par2->getGenerationIdx() +1 ;
}

FNode::FNode(Operator *oper, FNode *par1, bool maybe_neg, bool maybe_z)
        : prefactor(1.0), value(0.0), instruction(NULL), 
          maybe_negative(maybe_neg), maybe_zero(maybe_z), 
          op(oper), is_root(false) {
    parents.push_back(par1);
    generation_idx = par1->getGenerationIdx()+1;
}

FNode::~FNode() {
    if (instruction) delete instruction;
}

double &FNode::evaluate() {
    value = prefactor*op->evaluate(parents);
    return value;
}

std::string FNode::calculateTag() {
    if (is_root || tag != "") { ; }
    else {
        tag = op->getTag()+"(";
        for (auto p : parents) {
            tag += p->calculateTag() + ",";
        }
        tag += ")";
    }
    return tag;
}

bool FNode::containsOperator(std::string optag) {
    bool contains = false;
    if (op->getTag() == optag) contains = true;
    else {
        for (auto par: parents) {
            if (par->containsOperator(optag)) {
                contains = true; 
                break;
            }
        }
    }
    return contains;
}

Instruction *FNode::getOrCalculateInstruction() {
    if (instruction != NULL) {
        ;
    }
    else if (is_root) {
        instruction = new Instruction(op, tag, 1.0, 1.0);
    } else {
        std::vector<Instruction*> args;
        for (auto p: parents) args.push_back(p->getOrCalculateInstruction());
        for (auto arg: args) if (arg == NULL) throw soap::base::SanityCheckFailed(
            "Null instruction dependency: Need to generate instructions for parents first.");
        instruction = new Instruction(op, args);
    }
    return instruction;
}

// ===========
// Instruction
// ===========

Instruction::Instruction(Operator *oper, std::string tagstr, double pow, double prefactor)
        : op(oper), tag(tagstr), prefactor(prefactor), power(pow), is_root(true), expr("") {
}

Instruction::Instruction(Operator *oper, std::vector<Instruction*> &args_in)
        : op(oper), prefactor(1.0), power(1.0), expr(""), is_root(false) {
    for (auto a: args_in)
        args.push_back((new Instruction())->deepCopy(a));
    // Resolve operators: ":" -> "*" and "-" -> "+"
    if (op->getTag() == ":") {
        assert(args.size() == 2 && "Invalid number of arguments in division");
        args[1]->raiseToPower(-1);
        op = OP_MAP::get("*");
    } else if (op->getTag() == "-") {
        assert(args.size() == 2 && "Invalid number of arguments in subtraction");
        args[1]->multiplyBy(-1);
        op = OP_MAP::get("+");
    } else if (op->getTag() == "r") {
        assert(args.size() == 1 && "Invalid number of arguments in power");
        this->deepCopy(args[0]);
        this->raiseToPower(-1);
    } else if (op->getTag() == "s") {
        assert(args.size() == 1 && "Invalid number of arguments in power");
        this->deepCopy(args[0]);
        this->raiseToPower(0.5);
    } else if (op->getTag() == "2") {
        assert(args.size() == 1 && "Invalid number of arguments in power");
        this->deepCopy(args[0]);
        this->raiseToPower(2);
    }
    // Unpack arguments, for example *(a,b) = *(*(a1,a2),+(b1,b2)) -> *(a1,a2,+(b1,b2))
    if (OP_COMMUTES[op->getTag()]) {
        std::vector<Instruction*> args_unpacked;
        for (auto arg: args) {
            if (arg->op->getTag() == op->getTag()) {
                for (auto subarg: arg->args) args_unpacked.push_back(subarg);
            } else {
                args_unpacked.push_back(arg);
            }
        }
        args = args_unpacked;
    }
    // Simplify
    if (op->getTag() == "*") {
        // Add exponents of matching variables
        std::map<std::string, Instruction*> argmap;
        std::vector<Instruction *> args_short;
        for (auto a: args) {
            std::string argstr = a->getBasename();
            auto it = argmap.find(argstr);
            if (it != argmap.end()) {
                it->second->prefactor *= a->prefactor;
                it->second->power += a->power;
                delete a;
            } 
            else {
                argmap[argstr] = a;
                args_short.push_back(a);
            }
        }
        // Discard factors with exponent zero
        args = args_short;
        args_short.clear();
        for (auto a: args) {
            if (std::abs(a->power) < 1e-10) prefactor *= a->prefactor;
            else args_short.push_back(a);
        }
        args = args_short;
        // Single argument left
        if (args.size() == 1) {
            args[0]->raiseToPower(power);
            args[0]->multiplyBy(prefactor);
            this->deepCopy(args[0]);
        } else if (args.size() == 0) {
            is_root = true;
            tag = "1";
            power = 1.0;
        }
    }
    std::sort(args.begin(), args.end(), [](Instruction *i1, Instruction *i2) {
        return i1->getBasename() <= i2->getBasename(); 
    });
}

void Instruction::raiseToPower(double p) {
    if (op->getTag() == "*" || op->getTag() == ":") { 
        for (auto arg: args) arg->raiseToPower(p);
    } else {
        prefactor = std::pow(prefactor, p);
        power *= p;
    }
}

bool Instruction::containsConstant() {
    bool contains = false;
    if (is_root && tag == "1") contains = true;
    else {
        for (auto arg: args) {
            if (arg->containsConstant()) {
                contains = true; 
                break;
            }
        }
    }
    return contains;
}

std::string Instruction::getBasename() {
    std::string base = "";
    if (is_root) {
        base = "";
        base += (boost::format("%1$s") % tag).str();
    } else {
        std::vector<std::string> argstrs;
        for (auto a: args) {
            std::string format_str = 
                (args.size() > 1 && OP_PRIORITY[op->getTag()] > OP_PRIORITY[a->op->getTag()]) ?
                "(%1$s)" : "";
            argstrs.push_back(a->stringify(format_str));
        }
        base = op->format(argstrs);
    }
    return base;
}

std::string Instruction::stringify(std::string format) {
    if (expr != "") {
        ;
    } else {
        expr = this->getBasename();
        std::string prestr = (std::abs(prefactor-1.0) > 1e-20) ? 
            (boost::format("%1$+1.0f*") % prefactor).str() : "";
        std::string powstr = (std::abs(power-1.0) > 1e-20) ?
            (boost::format("^%1$1.1f") % power).str() : "";
        if (prestr+powstr != "")
            if (is_root || (OP_PRIORITY["*"] <= OP_PRIORITY[op->getTag()]) )
                expr = (boost::format("%1$s%2$s%3$s") % prestr % expr % powstr).str();
            else
            expr = (boost::format("%1$s(%2$s)%3$s") % prestr % expr % powstr).str();
    }
    if (format != "") expr = (boost::format(format) % expr).str();
    return expr;
}

Instruction *Instruction::deepCopy(Instruction *in) {
    op = in->op;
    tag = in->tag;
    power = in->power;
    is_root = in->is_root;
    prefactor = in->prefactor;
    std::vector<Instruction *> args_out;
    for (auto arg: in->args) args_out.push_back((new Instruction())->deepCopy(arg));
    for (auto arg: args) delete arg;
    args = args_out;
    return this;
}

Instruction::~Instruction() {
    for (auto it=args.begin(); it!=args.end(); ++it) delete *it;
}

// ======
// FGraph
// ======

bool FNodeCheck::check(FNode* fnode) {
    bool ok = true;
    auto dim = fnode->getDimension();
    for (auto it=dim.dim_map.begin(); it!=dim.dim_map.end(); ++it) {
        if (std::abs(it->second) < min_pow || std::abs(it->second) > max_pow) {
            ok = false;
            break;
        }
    }
    return ok;
}

FGraph::FGraph() {
    GLOG() << "Creating FGraph" << std::endl;
    // Unary ops
    uop_map["I"] = OP_MAP::get("I"); 
    uop_map["e"] = OP_MAP::get("e"); 
    uop_map["l"] = OP_MAP::get("l"); 
    uop_map["|"] = OP_MAP::get("|"); 
    uop_map["s"] = OP_MAP::get("s"); 
    uop_map["r"] = OP_MAP::get("r"); 
    uop_map["2"] = OP_MAP::get("2"); 
    // Binary ops
    bop_map["+"] = OP_MAP::get("+"); 
    bop_map["-"] = OP_MAP::get("-"); 
    bop_map["*"] = OP_MAP::get("*"); 
    bop_map[":"] = OP_MAP::get(":"); 
}

FGraph::~FGraph() {
    for (auto it=fnodes.begin(); it!=fnodes.end(); ++it) delete *it;
}

void FGraph::addRootNode(std::string varname, std::string maybe_neg, 
        std::string maybe_zero, std::string unit) {
    bool is_root = true;
    FNode *new_node = new FNode(uop_map["I"], varname, maybe_neg, maybe_zero, unit, is_root);
    root_fnodes.push_back(new_node);
    this->registerNewNode(new_node);
}

void FGraph::addLayer(std::string uops_str, std::string bops_str) {
    op_vec_t uop_layer;
    op_vec_t bop_layer;
    for (int i=0; i<uops_str.size(); ++i) {
        auto found = uop_map.find(std::string(1, uops_str[i]));
        if (found == uop_map.end()) throw soap::base::OutOfRange("Unary op" +  uops_str[i]);
        uop_layer.push_back(found->second);
    }
    for (int i=0; i<bops_str.size(); ++i) {
        auto found = bop_map.find(std::string(1, bops_str[i]));
        if (found == bop_map.end()) throw soap::base::OutOfRange("Binary op" + bops_str[i]);
        bop_layer.push_back(found->second);
    }
    uop_layers.push_back(uop_layer);
    bop_layers.push_back(bop_layer);
}

void FGraph::generateLayer(op_vec_t &uops, op_vec_t &bops) {
    FNodeCheck fnode_check(0.25, 4.0);
    // Unary layer
    std::vector<FNode*> new_nodes;
    for (auto fnode : fnodes) {
        for (auto uop : uops) {
            FNode *new_node = uop->generateAndCheck(fnode, fnode_check);
            if (new_node != NULL) new_nodes.push_back(new_node);
        }
    }
    for (auto new_node : new_nodes) this->registerNewNode(new_node);
    GLOG() << fnodes.size() << " after unary layer" << std::endl;
    // Binary layer
    new_nodes.clear();
    auto it1 = fnodes.begin();
    auto it2 = fnodes.begin();
    for (auto bop : bops) {
        GLOG() << "Operator " << bop->getTag() << std::endl;
        for (it1=fnodes.begin(); it1!=fnodes.end(); ++it1) {
            for (it2=it1+1; it2!=fnodes.end(); ++it2) {
                FNode *new_node = bop->generateAndCheck(*it1, *it2, fnode_check);
                if (new_node != NULL) new_nodes.push_back(new_node);
            }
        }
    }
    for (auto new_node : new_nodes) this->registerNewNode(new_node);
    GLOG() << fnodes.size() << " after binary layer" << std::endl;
    return;
}

void FGraph::generate() {
    GLOG() << "Generating graph from " << this->root_fnodes.size() << " root nodes" << std::endl;
    int n_layers = uop_layers.size();
    for (int n=0; n<n_layers; ++n) {
        op_vec_t &uops = uop_layers[n];
        op_vec_t &bops = bop_layers[n];
        GLOG() << "Layer " << (n+1) << ": Unary ops = [";
        for (auto it=uops.begin(); it!=uops.end(); ++it) GLOG() << (*it)->getTag();
        GLOG() << "] Binary ops = [";
        for (auto it=bops.begin(); it!=bops.end(); ++it) GLOG() << (*it)->getTag();
        GLOG() << "]" << std::endl;
        this->generateLayer(uops, bops);
        GLOG() << "Layer " << (n+1) << " done." << std::endl;
    }
    GLOG() << "Have a total of " << fnodes.size() << " nodes" << std::endl;
}

void FGraph::registerNewNode(FNode *new_node) {
    // Exclude duplicates and expressions with constants
    Instruction *in = new_node->getOrCalculateInstruction();
    std::string expr = in->stringify();
    bool keep = true;
    std::string mssg = "";
    if (in->containsConstant()) {
        keep = false;
        mssg = "[const]";
    } else {
        auto it = fnode_map.find(expr);
        if (it != fnode_map.end()) {
            keep = false;
            mssg = "[dupli]";
        }
    }
    GLOG() << (boost::format("%1$10s %2$50s == %3$-50s") 
        % mssg % new_node->calculateTag() % expr) << std::endl;
    // Add node to containers or delete
    if (keep) {
        fnodes.push_back(new_node);
        fnode_map[expr] = new_node;
    } else {
        delete new_node;
    }
}

boost::python::object FGraph::applyNumpy(boost::python::object &np_input, std::string np_dtype) {
    // TODO The copying to and from numpy causes significant delays. Find workaround.
    matrix_t input;
    soap::linalg::numpy_converter npc(np_dtype.c_str());
    npc.numpy_to_ublas<dtype_t>(np_input, input);
    matrix_t output = zero_matrix_t(input.size1(), fnodes.size());
    this->apply(input, output);
    return npc.ublas_to_numpy<dtype_t>(output);
}

void FGraph::apply(matrix_t &input, matrix_t &output) {
    assert(input.size2() == root_fnodes.size() && "Input size inconsistent with graph");
    // NOTE It is important that we evaluate all nodes in the order in
    // which they are stored in this->fnodes, i.e., in the order in which
    // they were originally generated. Otherwise value inconsistencies can
    // arise.
    for (int i=0; i<input.size1(); ++i) {
        for (int r=0; r<root_fnodes.size(); ++r) {
            root_fnodes[r]->seed(input(i,r));
            output(i,r) = input(i,r);
        }
        for (int f=root_fnodes.size(); f<fnodes.size(); ++f) {
            output(i,f) = fnodes[f]->evaluate();
        }
    }
}

void FGraph::registerPython() {
    using namespace boost::python;
    class_<FGraph>("FGraph", init<>())
        .def("addRootNode", &FGraph::addRootNode)
        .def("addLayer", &FGraph::addLayer)
        .def("generate", &FGraph::generate)
        .def("apply", &FGraph::applyNumpy);
}

}}


