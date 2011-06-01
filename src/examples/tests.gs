fix f => f (fix f)

fix (λ x. x)
= (λ x. x) (fix (λ x. x))
= for (x = fix (λ x. x)) x
= fix (λ x. x)


int		α → β		[α]
⌊int#⌋	⌊α →# β⌋		⌊[#α#]⌋

module 'list ∝ data 'α.
    | nil
    | α : list.t α
;

data 'α.
    | nil
    | α : list.t α
= 〈
    real.data.keyword 'unboxed 'α =
        | nil
        | α : list.t α
    ;
    type 't 'α = ⌊unboxed α⌋;
〉 :: 〈
    type t :: * → *;
    nil, pattern nil, :: ∀ 'α. t α;
    :, pattern :, :: ∀ 'α. α → t α → t α;
〉

module 'test.state ∝ data.
    | refuted
    | not.demonstrated
    | not.refuted
    | demonstrated
;

module 'test.step ∝ newtype 'm 'α. class m 〈
    run :: m 《 test.state.t, α, 》;
    teardown :: m 〈〉;
〉;
instance (module functor.c) test.step 'm | module monad.c m = 〈
    'fmap = around %run ∘ functor.c.fmap ∘ ?1;
〉;

module 'test.process ∝ data 'f 'α.
    | return α
    | step (f (test.process f α))
;
instance (module monad.c) test.process.t 'm | module functor.c m = monad.asHaskell 〈
    'return = test.process.return;
    ('>>=) = λ 'a 'f. analyze a
        case test.process.return 'x. f x
        case test.process.step 'step. step $ (>>= f) <$> step
    ;
〉;

type 'test 'm = test.process.t (test.process.t (test.step.t m)) 〈〉;

sig atomic.step :: ∀ 'f 'α. f α → test.process.t f α;
'atomic.step = test.process.step ∘ (fmap test.process.return);

sig test.∧ :: ∀ 'm. monad.rec.t m → test m → test m → test m;
implicit test.∧ (instance (module monad.c));
'test.∧ = λ (module 'mon) 't0 't1. analyze 《 t0, t1, 》
    case 《 