module 'gsprefix.(
    type gsprefix.t = 't,
    ... \
    gsprefix.in,
    gsprefix.out,
) ∝ 〈
    module 'gsprefix ∝ newtype. gsgenerator.t;
    module 'gsgenerator ∝ data.
        | gssig [gsname.t] gstype.t
        | gslet gspat.t gsexpr.t [gsexpr.t]
        | gsmatch gspat.t gsexpr.t [gsexpr.t]
    ;
    sig gsgenerator :: parser.t rune gsgenerator.t;
    'gsgenerator
       =  (λ _ 'nms _ 'ty. gsgenerator.gssig nms ty)
              <$> gslex.keyword m/sig/
              <*> gslex.sequence gslex.name (gslex.lexeme m/,/)
              ) <*> gslex.keywordOp m/::/
              <*> gstype.gstype gslex.maxPrec gslex.nonassoc
      <|> (λ 'p _ 'e. gsgenerator.gslet p e)
            <$> gsexpr.gspat (prec binding.precedence) gslex.nonassoc
            <*> gslex.keywordOp m/=/
            <*> gsexpr.gsexpr (prec binding.precedence) gslex.nonassoc
      <|> (λ 'p _ 'e. gsgenerator.gslet p e [])
            <$> gsexpr.gspat (prec binding.precedence) gslex.nonassoc
            <*> gslex.keywordOp m/=/
            <*> gsexpr.gsexpr (prec binding.precedence) gslex.nonassoc
〉
(where
    'binding.precedence = 5; -- Same as ≡ and ≠ operators
)
