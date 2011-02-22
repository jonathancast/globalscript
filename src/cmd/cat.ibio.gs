#! /home/jcast/globalscript/bin/ibio

for ('as ← env.arguments) analyze as
    case nil. cat
    case _. forM as $ λ 'a. cat << fn<§a>
(where
    'cat = for ('s ← parse $ many symbol; 't ← sync.tag.input.get;) print s *> sync.wait t;
    'many = λ 'a. for ('m = return [] <|> (:) <$> a <*> m) m;
    'forM = λ 'xn 'f. sequence (map f xn);
    'map = λ 'f. foldr ((:) · f) [];
    'sequence = foldr (λ 'a 'as. (:) <$> a <*> as) (return []);
)
