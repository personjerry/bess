package main

import (
	"fmt"
	"math"
	"sort"
	"strconv"
)

const end_symbol rune = 1114112

/* The rule types inferred from the grammar are below. */
type pegRule uint8

const (
	ruleUnknown pegRule = iota
	rulePolicy
	ruleConnection
	ruleCheck
	ruleChain
	ruleFlow
	ruleprefix
	rulemask
	ruleprobe
	ruledelim
	rulews

	rulePre_
	rule_In_
	rule_Suf
)

var rul3s = [...]string{
	"Unknown",
	"Policy",
	"Connection",
	"Check",
	"Chain",
	"Flow",
	"prefix",
	"mask",
	"probe",
	"delim",
	"ws",

	"Pre_",
	"_In_",
	"_Suf",
}

type tokenTree interface {
	Print()
	PrintSyntax()
	PrintSyntaxTree(buffer string)
	Add(rule pegRule, begin, end, next uint32, depth int)
	Expand(index int) tokenTree
	Tokens() <-chan token32
	AST() *node32
	Error() []token32
	trim(length int)
}

type node32 struct {
	token32
	up, next *node32
}

func (node *node32) print(depth int, buffer string) {
	for node != nil {
		for c := 0; c < depth; c++ {
			fmt.Printf(" ")
		}
		fmt.Printf("\x1B[34m%v\x1B[m %v\n", rul3s[node.pegRule], strconv.Quote(string(([]rune(buffer)[node.begin:node.end]))))
		if node.up != nil {
			node.up.print(depth+1, buffer)
		}
		node = node.next
	}
}

func (ast *node32) Print(buffer string) {
	ast.print(0, buffer)
}

type element struct {
	node *node32
	down *element
}

/* ${@} bit structure for abstract syntax tree */
type token32 struct {
	pegRule
	begin, end, next uint32
}

func (t *token32) isZero() bool {
	return t.pegRule == ruleUnknown && t.begin == 0 && t.end == 0 && t.next == 0
}

func (t *token32) isParentOf(u token32) bool {
	return t.begin <= u.begin && t.end >= u.end && t.next > u.next
}

func (t *token32) getToken32() token32 {
	return token32{pegRule: t.pegRule, begin: uint32(t.begin), end: uint32(t.end), next: uint32(t.next)}
}

func (t *token32) String() string {
	return fmt.Sprintf("\x1B[34m%v\x1B[m %v %v %v", rul3s[t.pegRule], t.begin, t.end, t.next)
}

type tokens32 struct {
	tree    []token32
	ordered [][]token32
}

func (t *tokens32) trim(length int) {
	t.tree = t.tree[0:length]
}

func (t *tokens32) Print() {
	for _, token := range t.tree {
		fmt.Println(token.String())
	}
}

func (t *tokens32) Order() [][]token32 {
	if t.ordered != nil {
		return t.ordered
	}

	depths := make([]int32, 1, math.MaxInt16)
	for i, token := range t.tree {
		if token.pegRule == ruleUnknown {
			t.tree = t.tree[:i]
			break
		}
		depth := int(token.next)
		if length := len(depths); depth >= length {
			depths = depths[:depth+1]
		}
		depths[depth]++
	}
	depths = append(depths, 0)

	ordered, pool := make([][]token32, len(depths)), make([]token32, len(t.tree)+len(depths))
	for i, depth := range depths {
		depth++
		ordered[i], pool, depths[i] = pool[:depth], pool[depth:], 0
	}

	for i, token := range t.tree {
		depth := token.next
		token.next = uint32(i)
		ordered[depth][depths[depth]] = token
		depths[depth]++
	}
	t.ordered = ordered
	return ordered
}

type state32 struct {
	token32
	depths []int32
	leaf   bool
}

func (t *tokens32) AST() *node32 {
	tokens := t.Tokens()
	stack := &element{node: &node32{token32: <-tokens}}
	for token := range tokens {
		if token.begin == token.end {
			continue
		}
		node := &node32{token32: token}
		for stack != nil && stack.node.begin >= token.begin && stack.node.end <= token.end {
			stack.node.next = node.up
			node.up = stack.node
			stack = stack.down
		}
		stack = &element{node: node, down: stack}
	}
	return stack.node
}

func (t *tokens32) PreOrder() (<-chan state32, [][]token32) {
	s, ordered := make(chan state32, 6), t.Order()
	go func() {
		var states [8]state32
		for i := range states {
			states[i].depths = make([]int32, len(ordered))
		}
		depths, state, depth := make([]int32, len(ordered)), 0, 1
		write := func(t token32, leaf bool) {
			S := states[state]
			state, S.pegRule, S.begin, S.end, S.next, S.leaf = (state+1)%8, t.pegRule, t.begin, t.end, uint32(depth), leaf
			copy(S.depths, depths)
			s <- S
		}

		states[state].token32 = ordered[0][0]
		depths[0]++
		state++
		a, b := ordered[depth-1][depths[depth-1]-1], ordered[depth][depths[depth]]
	depthFirstSearch:
		for {
			for {
				if i := depths[depth]; i > 0 {
					if c, j := ordered[depth][i-1], depths[depth-1]; a.isParentOf(c) &&
						(j < 2 || !ordered[depth-1][j-2].isParentOf(c)) {
						if c.end != b.begin {
							write(token32{pegRule: rule_In_, begin: c.end, end: b.begin}, true)
						}
						break
					}
				}

				if a.begin < b.begin {
					write(token32{pegRule: rulePre_, begin: a.begin, end: b.begin}, true)
				}
				break
			}

			next := depth + 1
			if c := ordered[next][depths[next]]; c.pegRule != ruleUnknown && b.isParentOf(c) {
				write(b, false)
				depths[depth]++
				depth, a, b = next, b, c
				continue
			}

			write(b, true)
			depths[depth]++
			c, parent := ordered[depth][depths[depth]], true
			for {
				if c.pegRule != ruleUnknown && a.isParentOf(c) {
					b = c
					continue depthFirstSearch
				} else if parent && b.end != a.end {
					write(token32{pegRule: rule_Suf, begin: b.end, end: a.end}, true)
				}

				depth--
				if depth > 0 {
					a, b, c = ordered[depth-1][depths[depth-1]-1], a, ordered[depth][depths[depth]]
					parent = a.isParentOf(b)
					continue
				}

				break depthFirstSearch
			}
		}

		close(s)
	}()
	return s, ordered
}

func (t *tokens32) PrintSyntax() {
	tokens, ordered := t.PreOrder()
	max := -1
	for token := range tokens {
		if !token.leaf {
			fmt.Printf("%v", token.begin)
			for i, leaf, depths := 0, int(token.next), token.depths; i < leaf; i++ {
				fmt.Printf(" \x1B[36m%v\x1B[m", rul3s[ordered[i][depths[i]-1].pegRule])
			}
			fmt.Printf(" \x1B[36m%v\x1B[m\n", rul3s[token.pegRule])
		} else if token.begin == token.end {
			fmt.Printf("%v", token.begin)
			for i, leaf, depths := 0, int(token.next), token.depths; i < leaf; i++ {
				fmt.Printf(" \x1B[31m%v\x1B[m", rul3s[ordered[i][depths[i]-1].pegRule])
			}
			fmt.Printf(" \x1B[31m%v\x1B[m\n", rul3s[token.pegRule])
		} else {
			for c, end := token.begin, token.end; c < end; c++ {
				if i := int(c); max+1 < i {
					for j := max; j < i; j++ {
						fmt.Printf("skip %v %v\n", j, token.String())
					}
					max = i
				} else if i := int(c); i <= max {
					for j := i; j <= max; j++ {
						fmt.Printf("dupe %v %v\n", j, token.String())
					}
				} else {
					max = int(c)
				}
				fmt.Printf("%v", c)
				for i, leaf, depths := 0, int(token.next), token.depths; i < leaf; i++ {
					fmt.Printf(" \x1B[34m%v\x1B[m", rul3s[ordered[i][depths[i]-1].pegRule])
				}
				fmt.Printf(" \x1B[34m%v\x1B[m\n", rul3s[token.pegRule])
			}
			fmt.Printf("\n")
		}
	}
}

func (t *tokens32) PrintSyntaxTree(buffer string) {
	tokens, _ := t.PreOrder()
	for token := range tokens {
		for c := 0; c < int(token.next); c++ {
			fmt.Printf(" ")
		}
		fmt.Printf("\x1B[34m%v\x1B[m %v\n", rul3s[token.pegRule], strconv.Quote(string(([]rune(buffer)[token.begin:token.end]))))
	}
}

func (t *tokens32) Add(rule pegRule, begin, end, depth uint32, index int) {
	t.tree[index] = token32{pegRule: rule, begin: uint32(begin), end: uint32(end), next: uint32(depth)}
}

func (t *tokens32) Tokens() <-chan token32 {
	s := make(chan token32, 16)
	go func() {
		for _, v := range t.tree {
			s <- v.getToken32()
		}
		close(s)
	}()
	return s
}

func (t *tokens32) Error() []token32 {
	ordered := t.Order()
	length := len(ordered)
	tokens, length := make([]token32, length), length-1
	for i := range tokens {
		o := ordered[length-i]
		if len(o) > 1 {
			tokens[i] = o[len(o)-2].getToken32()
		}
	}
	return tokens
}

/*func (t *tokens16) Expand(index int) tokenTree {
	tree := t.tree
	if index >= len(tree) {
		expanded := make([]token32, 2 * len(tree))
		for i, v := range tree {
			expanded[i] = v.getToken32()
		}
		return &tokens32{tree: expanded}
	}
	return nil
}*/

func (t *tokens32) Expand(index int) tokenTree {
	tree := t.tree
	if index >= len(tree) {
		expanded := make([]token32, 2*len(tree))
		copy(expanded, tree)
		t.tree = expanded
	}
	return nil
}

type Policy struct {
	Buffer string
	buffer []rune
	rules  [11]func() bool
	Parse  func(rule ...int) error
	Reset  func()
	tokenTree
}

type textPosition struct {
	line, symbol int
}

type textPositionMap map[int]textPosition

func translatePositions(buffer string, positions []int) textPositionMap {
	length, translations, j, line, symbol := len(positions), make(textPositionMap, len(positions)), 0, 1, 0
	sort.Ints(positions)

search:
	for i, c := range []rune(buffer) {
		if c == '\n' {
			line, symbol = line+1, 0
		} else {
			symbol++
		}
		if i == positions[j] {
			translations[positions[j]] = textPosition{line, symbol}
			for j++; j < length; j++ {
				if i != positions[j] {
					continue search
				}
			}
			break search
		}
	}

	return translations
}

type parseError struct {
	p *Policy
}

func (e *parseError) Error() string {
	tokens, error := e.p.tokenTree.Error(), "\n"
	positions, p := make([]int, 2*len(tokens)), 0
	for _, token := range tokens {
		positions[p], p = int(token.begin), p+1
		positions[p], p = int(token.end), p+1
	}
	translations := translatePositions(e.p.Buffer, positions)
	for _, token := range tokens {
		begin, end := int(token.begin), int(token.end)
		error += fmt.Sprintf("parse error near \x1B[34m%v\x1B[m (line %v symbol %v - line %v symbol %v):\n%v\n",
			rul3s[token.pegRule],
			translations[begin].line, translations[begin].symbol,
			translations[end].line, translations[end].symbol,
			/*strconv.Quote(*/ e.p.Buffer[begin:end] /*)*/)
	}

	return error
}

func (p *Policy) PrintSyntaxTree() {
	p.tokenTree.PrintSyntaxTree(p.Buffer)
}

func (p *Policy) Highlighter() {
	p.tokenTree.PrintSyntax()
}

func (p *Policy) Init() {
	p.buffer = []rune(p.Buffer)
	if len(p.buffer) == 0 || p.buffer[len(p.buffer)-1] != end_symbol {
		p.buffer = append(p.buffer, end_symbol)
	}

	var tree tokenTree = &tokens32{tree: make([]token32, math.MaxInt16)}
	position, depth, tokenIndex, buffer, _rules := uint32(0), uint32(0), 0, p.buffer, p.rules

	p.Parse = func(rule ...int) error {
		r := 1
		if len(rule) > 0 {
			r = rule[0]
		}
		matches := p.rules[r]()
		p.tokenTree = tree
		if matches {
			p.tokenTree.trim(tokenIndex)
			return nil
		}
		return &parseError{p}
	}

	p.Reset = func() {
		position, tokenIndex, depth = 0, 0, 0
	}

	add := func(rule pegRule, begin uint32) {
		if t := tree.Expand(tokenIndex); t != nil {
			tree = t
		}
		tree.Add(rule, begin, position, depth, tokenIndex)
		tokenIndex++
	}

	matchDot := func() bool {
		if buffer[position] != end_symbol {
			position++
			return true
		}
		return false
	}

	/*matchChar := func(c byte) bool {
		if buffer[position] == c {
			position++
			return true
		}
		return false
	}*/

	/*matchRange := func(lower byte, upper byte) bool {
		if c := buffer[position]; c >= lower && c <= upper {
			position++
			return true
		}
		return false
	}*/

	_rules = [...]func() bool{
		nil,
		/* 0 Policy <- <(ws Connection+ ws Check+ !.)> */
		func() bool {
			position0, tokenIndex0, depth0 := position, tokenIndex, depth
			{
				position1 := position
				depth++
				if !_rules[rulews]() {
					goto l0
				}
				if !_rules[ruleConnection]() {
					goto l0
				}
			l2:
				{
					position3, tokenIndex3, depth3 := position, tokenIndex, depth
					if !_rules[ruleConnection]() {
						goto l3
					}
					goto l2
				l3:
					position, tokenIndex, depth = position3, tokenIndex3, depth3
				}
				if !_rules[rulews]() {
					goto l0
				}
				if !_rules[ruleCheck]() {
					goto l0
				}
			l4:
				{
					position5, tokenIndex5, depth5 := position, tokenIndex, depth
					if !_rules[ruleCheck]() {
						goto l5
					}
					goto l4
				l5:
					position, tokenIndex, depth = position5, tokenIndex5, depth5
				}
				{
					position6, tokenIndex6, depth6 := position, tokenIndex, depth
					if !matchDot() {
						goto l6
					}
					goto l0
				l6:
					position, tokenIndex, depth = position6, tokenIndex6, depth6
				}
				depth--
				add(rulePolicy, position1)
			}
			return true
		l0:
			position, tokenIndex, depth = position0, tokenIndex0, depth0
			return false
		},
		/* 1 Connection <- <(probe ws ('-' '>') ws probe ws)> */
		func() bool {
			position7, tokenIndex7, depth7 := position, tokenIndex, depth
			{
				position8 := position
				depth++
				if !_rules[ruleprobe]() {
					goto l7
				}
				if !_rules[rulews]() {
					goto l7
				}
				if buffer[position] != rune('-') {
					goto l7
				}
				position++
				if buffer[position] != rune('>') {
					goto l7
				}
				position++
				if !_rules[rulews]() {
					goto l7
				}
				if !_rules[ruleprobe]() {
					goto l7
				}
				if !_rules[rulews]() {
					goto l7
				}
				depth--
				add(ruleConnection, position8)
			}
			return true
		l7:
			position, tokenIndex, depth = position7, tokenIndex7, depth7
			return false
		},
		/* 2 Check <- <(Flow ws ':' ws Chain '\n')> */
		func() bool {
			position9, tokenIndex9, depth9 := position, tokenIndex, depth
			{
				position10 := position
				depth++
				if !_rules[ruleFlow]() {
					goto l9
				}
				if !_rules[rulews]() {
					goto l9
				}
				if buffer[position] != rune(':') {
					goto l9
				}
				position++
				if !_rules[rulews]() {
					goto l9
				}
				if !_rules[ruleChain]() {
					goto l9
				}
				if buffer[position] != rune('\n') {
					goto l9
				}
				position++
				depth--
				add(ruleCheck, position10)
			}
			return true
		l9:
			position, tokenIndex, depth = position9, tokenIndex9, depth9
			return false
		},
		/* 3 Chain <- <((probe ws ('-' '>') ws Chain) / probe)> */
		func() bool {
			position11, tokenIndex11, depth11 := position, tokenIndex, depth
			{
				position12 := position
				depth++
				{
					position13, tokenIndex13, depth13 := position, tokenIndex, depth
					if !_rules[ruleprobe]() {
						goto l14
					}
					if !_rules[rulews]() {
						goto l14
					}
					if buffer[position] != rune('-') {
						goto l14
					}
					position++
					if buffer[position] != rune('>') {
						goto l14
					}
					position++
					if !_rules[rulews]() {
						goto l14
					}
					if !_rules[ruleChain]() {
						goto l14
					}
					goto l13
				l14:
					position, tokenIndex, depth = position13, tokenIndex13, depth13
					if !_rules[ruleprobe]() {
						goto l11
					}
				}
			l13:
				depth--
				add(ruleChain, position12)
			}
			return true
		l11:
			position, tokenIndex, depth = position11, tokenIndex11, depth11
			return false
		},
		/* 4 Flow <- <('(' prefix delim prefix delim probe delim probe delim probe ws ')')> */
		func() bool {
			position15, tokenIndex15, depth15 := position, tokenIndex, depth
			{
				position16 := position
				depth++
				if buffer[position] != rune('(') {
					goto l15
				}
				position++
				if !_rules[ruleprefix]() {
					goto l15
				}
				if !_rules[ruledelim]() {
					goto l15
				}
				if !_rules[ruleprefix]() {
					goto l15
				}
				if !_rules[ruledelim]() {
					goto l15
				}
				if !_rules[ruleprobe]() {
					goto l15
				}
				if !_rules[ruledelim]() {
					goto l15
				}
				if !_rules[ruleprobe]() {
					goto l15
				}
				if !_rules[ruledelim]() {
					goto l15
				}
				if !_rules[ruleprobe]() {
					goto l15
				}
				if !_rules[rulews]() {
					goto l15
				}
				if buffer[position] != rune(')') {
					goto l15
				}
				position++
				depth--
				add(ruleFlow, position16)
			}
			return true
		l15:
			position, tokenIndex, depth = position15, tokenIndex15, depth15
			return false
		},
		/* 5 prefix <- <([0-9]+ '.' [0-9]+ '.' [0-9]+ '.' [0-9]+ mask?)> */
		func() bool {
			position17, tokenIndex17, depth17 := position, tokenIndex, depth
			{
				position18 := position
				depth++
				if c := buffer[position]; c < rune('0') || c > rune('9') {
					goto l17
				}
				position++
			l19:
				{
					position20, tokenIndex20, depth20 := position, tokenIndex, depth
					if c := buffer[position]; c < rune('0') || c > rune('9') {
						goto l20
					}
					position++
					goto l19
				l20:
					position, tokenIndex, depth = position20, tokenIndex20, depth20
				}
				if buffer[position] != rune('.') {
					goto l17
				}
				position++
				if c := buffer[position]; c < rune('0') || c > rune('9') {
					goto l17
				}
				position++
			l21:
				{
					position22, tokenIndex22, depth22 := position, tokenIndex, depth
					if c := buffer[position]; c < rune('0') || c > rune('9') {
						goto l22
					}
					position++
					goto l21
				l22:
					position, tokenIndex, depth = position22, tokenIndex22, depth22
				}
				if buffer[position] != rune('.') {
					goto l17
				}
				position++
				if c := buffer[position]; c < rune('0') || c > rune('9') {
					goto l17
				}
				position++
			l23:
				{
					position24, tokenIndex24, depth24 := position, tokenIndex, depth
					if c := buffer[position]; c < rune('0') || c > rune('9') {
						goto l24
					}
					position++
					goto l23
				l24:
					position, tokenIndex, depth = position24, tokenIndex24, depth24
				}
				if buffer[position] != rune('.') {
					goto l17
				}
				position++
				if c := buffer[position]; c < rune('0') || c > rune('9') {
					goto l17
				}
				position++
			l25:
				{
					position26, tokenIndex26, depth26 := position, tokenIndex, depth
					if c := buffer[position]; c < rune('0') || c > rune('9') {
						goto l26
					}
					position++
					goto l25
				l26:
					position, tokenIndex, depth = position26, tokenIndex26, depth26
				}
				{
					position27, tokenIndex27, depth27 := position, tokenIndex, depth
					if !_rules[rulemask]() {
						goto l27
					}
					goto l28
				l27:
					position, tokenIndex, depth = position27, tokenIndex27, depth27
				}
			l28:
				depth--
				add(ruleprefix, position18)
			}
			return true
		l17:
			position, tokenIndex, depth = position17, tokenIndex17, depth17
			return false
		},
		/* 6 mask <- <('/' [0-9]+)> */
		func() bool {
			position29, tokenIndex29, depth29 := position, tokenIndex, depth
			{
				position30 := position
				depth++
				if buffer[position] != rune('/') {
					goto l29
				}
				position++
				if c := buffer[position]; c < rune('0') || c > rune('9') {
					goto l29
				}
				position++
			l31:
				{
					position32, tokenIndex32, depth32 := position, tokenIndex, depth
					if c := buffer[position]; c < rune('0') || c > rune('9') {
						goto l32
					}
					position++
					goto l31
				l32:
					position, tokenIndex, depth = position32, tokenIndex32, depth32
				}
				depth--
				add(rulemask, position30)
			}
			return true
		l29:
			position, tokenIndex, depth = position29, tokenIndex29, depth29
			return false
		},
		/* 7 probe <- <[0-9]+> */
		func() bool {
			position33, tokenIndex33, depth33 := position, tokenIndex, depth
			{
				position34 := position
				depth++
				if c := buffer[position]; c < rune('0') || c > rune('9') {
					goto l33
				}
				position++
			l35:
				{
					position36, tokenIndex36, depth36 := position, tokenIndex, depth
					if c := buffer[position]; c < rune('0') || c > rune('9') {
						goto l36
					}
					position++
					goto l35
				l36:
					position, tokenIndex, depth = position36, tokenIndex36, depth36
				}
				depth--
				add(ruleprobe, position34)
			}
			return true
		l33:
			position, tokenIndex, depth = position33, tokenIndex33, depth33
			return false
		},
		/* 8 delim <- <(ws ',' ws)> */
		func() bool {
			position37, tokenIndex37, depth37 := position, tokenIndex, depth
			{
				position38 := position
				depth++
				if !_rules[rulews]() {
					goto l37
				}
				if buffer[position] != rune(',') {
					goto l37
				}
				position++
				if !_rules[rulews]() {
					goto l37
				}
				depth--
				add(ruledelim, position38)
			}
			return true
		l37:
			position, tokenIndex, depth = position37, tokenIndex37, depth37
			return false
		},
		/* 9 ws <- <(' ' / '\t' / '\n' / '\r')*> */
		func() bool {
			{
				position40 := position
				depth++
			l41:
				{
					position42, tokenIndex42, depth42 := position, tokenIndex, depth
					{
						position43, tokenIndex43, depth43 := position, tokenIndex, depth
						if buffer[position] != rune(' ') {
							goto l44
						}
						position++
						goto l43
					l44:
						position, tokenIndex, depth = position43, tokenIndex43, depth43
						if buffer[position] != rune('\t') {
							goto l45
						}
						position++
						goto l43
					l45:
						position, tokenIndex, depth = position43, tokenIndex43, depth43
						if buffer[position] != rune('\n') {
							goto l46
						}
						position++
						goto l43
					l46:
						position, tokenIndex, depth = position43, tokenIndex43, depth43
						if buffer[position] != rune('\r') {
							goto l42
						}
						position++
					}
				l43:
					goto l41
				l42:
					position, tokenIndex, depth = position42, tokenIndex42, depth42
				}
				depth--
				add(rulews, position40)
			}
			return true
		},
	}
	p.rules = _rules
}
