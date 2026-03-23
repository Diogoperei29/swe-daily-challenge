use ring_buffer::RingBuffer;

fn main() {
    let mut rb: RingBuffer<String> = RingBuffer::new(4);

    rb.push("alpha".into()).unwrap();
    rb.push("beta".into()).unwrap();
    rb.push("gamma".into()).unwrap();

    println!("{:?}", rb.pop()); // Some("alpha")
    println!("{:?}", rb.pop()); // Some("beta")

    rb.push("delta".into()).unwrap();
    rb.push("epsilon".into()).unwrap();
    rb.push("zeta".into()).unwrap();    
    assert!(rb.push("omega".into()).is_err()); // now full → Err

    while let Some(s) = rb.pop() {
        println!("{s}"); // gamma, delta, epsilon, zeta
    }
    println!("{:?}", rb.pop()); // None
}
